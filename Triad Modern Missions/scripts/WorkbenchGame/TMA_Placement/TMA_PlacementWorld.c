#ifdef WORKBENCH
//! NET API of the placement tool (Tools/Placement/placement.py). The World Editor side only reads the open world
//! and applies transforms; all placement rules live in Python, so changing them needs no script reload.
//! Arguments are text lines of space separated values, answers are JSON text. The NET API (like string.Format and
//! Substring) cuts strings at 8191 characters, so long text is joined with "+" and an answer comes in parts:
//! "total" is its length, command "chunk" with the offset as args returns the next part of the last answer.
class TMA_PlacementRequest : JsonApiStruct
{
	string command;
	string args;

	void TMA_PlacementRequest()
	{
		RegV("command");
		RegV("args");
	}
}

class TMA_PlacementResponse : JsonApiStruct
{
	string json;
	int total;

	void TMA_PlacementResponse()
	{
		RegV("json");
		RegV("total");
	}
}

class TMA_PlacementHandlerBase : NetApiHandler
{
	protected static const int CHUNK = 3000;
	protected static string s_sAnswer;

	protected WorldEditorAPI m_API;
	protected BaseWorld m_World;
	protected int m_iSubScene;
	protected ref array<IEntity> m_aMission = {};

	//------------------------------------------------------------------------------------------------
	override JsonApiStruct GetRequest()
	{
		return new TMA_PlacementRequest();
	}

	//------------------------------------------------------------------------------------------------
	override JsonApiStruct GetResponse(JsonApiStruct request)
	{
		TMA_PlacementRequest req = TMA_PlacementRequest.Cast(request);
		TMA_PlacementResponse response = new TMA_PlacementResponse();
		if (!req)
		{
			response.json = ErrorJson("bad request");
			response.total = response.json.Length();
			return response;
		}

		int offset;
		if (req.command == "chunk")
		{
			offset = req.args.ToInt();
		}
		else if (!Init())
		{
			s_sAnswer = ErrorJson("World Editor has no open world");
		}
		else
		{
			s_sAnswer = Run(req.command, req.args);
		}

		response.total = s_sAnswer.Length();
		int size = response.total - offset;
		if (size > CHUNK)
			size = CHUNK;

		if (size > 0)
			response.json = s_sAnswer.Substring(offset, size);

		return response;
	}

	//------------------------------------------------------------------------------------------------
	protected string Run(string command, string args)
	{
		return ErrorJson("unknown command " + command);
	}

	//------------------------------------------------------------------------------------------------
	//! Mission entities are the ones of the open sub-scene (the episode layers), the map itself is the parent scene
	protected bool Init()
	{
		WorldEditor worldEditor = Workbench.GetModule(WorldEditor);
		if (!worldEditor)
			return false;

		m_API = worldEditor.GetApi();
		if (!m_API)
			return false;

		m_World = m_API.GetWorld();
		if (!m_World)
			return false;

		m_iSubScene = m_API.GetCurrentSubScene();
		m_aMission.Clear();
		for (int i, count = m_API.GetEntityCount(m_iSubScene); i < count; i++)
		{
			IEntity entity = m_API.SourceToEntity(m_API.GetEntity(m_iSubScene, i));
			if (entity)
				m_aMission.Insert(entity);
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Mission objects and characters never block a placement check, they are what is being placed
	protected bool Filter(notnull IEntity e, vector start = "0 0 0", vector dir = "0 0 0")
	{
		IEntity root = e.GetRootParent();
		if (ChimeraCharacter.Cast(root))
			return false;

		return !m_aMission.Contains(root);
	}

	//------------------------------------------------------------------------------------------------
	protected static array<string> Lines(string args)
	{
		array<string> lines = {};
		args.Split("\n", lines, true);
		return lines;
	}

	//------------------------------------------------------------------------------------------------
	protected static array<float> Numbers(string line)
	{
		array<string> parts = {};
		line.Split(" ", parts, true);
		array<float> values = {};
		foreach (string part : parts)
		{
			values.Insert(part.ToFloat());
		}

		return values;
	}

	//------------------------------------------------------------------------------------------------
	protected static string Num(float value)
	{
		return value.ToString(-1, 3);
	}

	//------------------------------------------------------------------------------------------------
	protected static string Vec(vector v)
	{
		return string.Format("[%1,%2,%3]", Num(v[0]), Num(v[1]), Num(v[2]));
	}

	//------------------------------------------------------------------------------------------------
	protected static string Bool(bool value)
	{
		if (value)
			return "true";

		return "false";
	}

	//------------------------------------------------------------------------------------------------
	protected static string Str(string text)
	{
		text.Replace("\\", "\\\\");
		text.Replace("\"", "\\\"");
		return "\"" + text + "\"";
	}

	//------------------------------------------------------------------------------------------------
	protected static string ErrorJson(string message)
	{
		return "{\"error\":" + Str(message) + "}";
	}

	//------------------------------------------------------------------------------------------------
	protected static string EntityLabel(IEntity entity)
	{
		if (!entity)
			return "";

		string prefab;
		EntityPrefabData data = entity.GetPrefabData();
		if (data)
			prefab = data.GetPrefabName();

		return string.Format("%1|%2|%3", entity.ClassName(), prefab, entity.GetName());
	}
}

class TMA_PlacementRoad
{
	RoadGeneratorEntity m_Generator;
	string m_sName;
	float m_fWidth;
	vector m_vMins;
	vector m_vMaxs;
	ref array<vector> m_aPoints = {};
}

//! Read-only questions about the open world:
//! - entities: top-level entities of the open sub-scene with transforms, bounds, squad links, formations, zones
//! - wheels: "<id>" per line, wheel positions (local) and radii of wheeled vehicles
//! - surface: "<x> <z>" per line, terrain height, the first surface hit from above (objects included), what was hit
//!   and its material ("terrain material|game material")
//! - members: "<group id>" per line, positions of the soldiers the group has spawned (empty in edit mode)
//! - spawn: "<x> <z>" per line, where a group would actually put a soldier standing there (terrain, navmesh)
//! - collide: "<pos 3> <row0 3> <row1 3> <row2 3> <mins 3> <maxs 3>" per line, first world object the box touches
//! - roads: "<minX> <minZ> <maxX> <maxZ>", centre lines and widths of the map's road generators inside the box
//! - locations: the named places of the map (prefabs under World/Locations, all scenes) with their positions
class TMA_PlacementQuery : TMA_PlacementHandlerBase
{
	protected static ref array<ref TMA_PlacementRoad> s_aRoads;
	protected static string s_sRoadsWorld;

	//------------------------------------------------------------------------------------------------
	override protected string Run(string command, string args)
	{
		if (command == "entities")
			return Entities();

		if (command == "wheels")
			return Wheels(args);

		if (command == "surface")
			return Surface(args);

		if (command == "collide")
			return Collide(args);

		if (command == "roads")
			return Roads(args);

		if (command == "members")
			return Members(args);

		if (command == "spawn")
			return Spawn(args);

		if (command == "locations")
			return Locations();

		return ErrorJson("unknown command " + command);
	}

	//------------------------------------------------------------------------------------------------
	//! Towns, villages and other named places: entities of the World/Locations prefabs in every scene.
	protected string Locations()
	{
		array<string> items = {};
		for (int scene, sceneCount = m_API.GetNumSubScenes(); scene < sceneCount; scene++)
		{
			for (int i, count = m_API.GetEntityCount(scene); i < count; i++)
			{
				IEntitySource source = m_API.GetEntity(scene, i);
				if (!source)
					continue;

				IEntity entity = m_API.SourceToEntity(source);
				if (!entity)
					continue;

				EntityPrefabData data = entity.GetPrefabData();
				if (!data)
					continue;

				string prefab = data.GetPrefabName();
				if (!prefab.Contains("/World/Locations/"))
					continue;

				items.Insert(string.Format("{\"prefab\":%1,\"name\":%2,\"pos\":%3}", Str(prefab), Str(entity.GetName()), Vec(entity.GetOrigin())));
			}
		}

		return "[" + SCR_StringHelper.Join(",", items) + "]";
	}

	//------------------------------------------------------------------------------------------------
	//! Where SCR_AIGroup.SpawnGroupMember puts a soldier standing at a point: on the terrain, then moved to the
	//! closest navmesh point and kept no lower than the terrain. "<x> <z>" per line; a group of the open scene
	//! lends its pathfinding component. Answer per line: [position, snapped to the navmesh].
	protected string Spawn(string args)
	{
		AIPathfindingComponent pathfinding;
		foreach (IEntity entity : m_aMission)
		{
			SCR_AIGroup group = SCR_AIGroup.Cast(entity);
			if (group)
				pathfinding = AIPathfindingComponent.Cast(group.FindComponent(AIPathfindingComponent));

			if (pathfinding)
				break;
		}

		array<string> items = {};
		array<string> lines = Lines(args);
		foreach (string line : lines)
		{
			array<float> v = Numbers(line);
			if (v.Count() < 2)
				continue;

			vector position = Vector(v[0], m_World.GetSurfaceY(v[0], v[1]), v[1]);
			bool snapped;
			if (pathfinding)
				snapped = pathfinding.GetClosestPositionOnNavmesh(position, Vector(10, 10, 10), position);

			if (snapped)
			{
				float ground = m_World.GetSurfaceY(position[0], position[2]);
				if (position[1] < ground)
					position[1] = ground;
			}

			items.Insert(string.Format("[%1,%2]", Vec(position), Bool(snapped)));
		}

		return "[" + SCR_StringHelper.Join(",", items) + "]";
	}

	//------------------------------------------------------------------------------------------------
	//! Where the soldiers of each group stand now: in World Editor these are the previews the group spawned,
	//! placed by SCR_AIGroup itself (formation, terrain and navmesh snapping)
	protected string Members(string args)
	{
		array<string> items = {};
		array<string> lines = Lines(args);
		foreach (string line : lines)
		{
			SCR_AIGroup group = SCR_AIGroup.Cast(m_API.SourceToEntity(m_API.GetEntity(m_iSubScene, line.ToInt())));
			array<string> list = {};
			if (group)
			{
				array<AIAgent> agents = {};
				group.GetAgents(agents);
				foreach (AIAgent agent : agents)
				{
					IEntity member;
					if (agent)
						member = agent.GetControlledEntity();

					if (member)
						list.Insert(Vec(member.GetOrigin()));
				}
			}

			items.Insert("[" + SCR_StringHelper.Join(",", list) + "]");
		}

		return "[" + SCR_StringHelper.Join(",", items) + "]";
	}

	//------------------------------------------------------------------------------------------------
	protected string Entities()
	{
		string worldPath;
		m_API.GetWorldPath(worldPath);

		AIWorld aiWorld = GetGame().GetAIWorld();
		array<string> items = {};
		for (int i, count = m_API.GetEntityCount(m_iSubScene); i < count; i++)
		{
			IEntitySource source = m_API.GetEntity(m_iSubScene, i);
			IEntity entity = m_API.SourceToEntity(source);
			if (entity && !entity.GetParent())
				items.Insert(EntityJson(i, source, entity, aiWorld));
		}

		return "{\"world\":" + Str(worldPath) + string.Format(",\"subScene\":%1,\"entities\":[", m_iSubScene) + SCR_StringHelper.Join(",", items) + "]}";
	}

	//------------------------------------------------------------------------------------------------
	protected string EntityJson(int id, IEntitySource source, IEntity entity, AIWorld aiWorld)
	{
		string className = source.GetClassName();
		typename type = className.ToType();

		array<string> fields = {};
		fields.Insert(string.Format("\"id\":%1", id));
		fields.Insert("\"name\":" + Str(source.GetName()));
		fields.Insert("\"cls\":" + Str(className));
		fields.Insert("\"prefab\":" + Str(SCR_BaseContainerTools.GetPrefabResourceName(source)));
		fields.Insert("\"layer\":" + Str(m_API.GetEntitySubsceneLayer(m_iSubScene, source)));
		fields.Insert(string.Format("\"layerId\":%1", source.GetLayerID()));

		vector transform[4];
		entity.GetWorldTransform(transform);
		vector rotation[3];
		rotation[0] = transform[0];
		rotation[1] = transform[1];
		rotation[2] = transform[2];
		vector angles;
		source.Get("angles", angles);
		fields.Insert("\"pos\":" + Vec(transform[3]));
		fields.Insert(string.Format("\"mat\":[%1,%2,%3]", Vec(transform[0]), Vec(transform[1]), Vec(transform[2])));
		fields.Insert("\"ypr\":" + Vec(Math3D.MatrixToAngles(rotation)));
		fields.Insert("\"angles\":" + Vec(angles));

		vector mins, maxs;
		entity.GetBounds(mins, maxs);
		fields.Insert("\"bmin\":" + Vec(mins));
		fields.Insert("\"bmax\":" + Vec(maxs));
		fields.Insert("\"physics\":" + Bool(entity.GetPhysics() != null));
		fields.Insert("\"storage\":" + Bool(SCR_BaseContainerTools.FindComponentSource(source, SCR_UniversalInventoryStorageComponent) != null));
		fields.Insert("\"wheeled\":" + Bool(entity.FindComponent(VehicleWheeledSimulation) || entity.FindComponent(VehicleWheeledSimulation_SA_B)));

		IEntityComponentSource link = SCR_BaseContainerTools.FindComponentSource(source, "LL_VehicleSquadLinkComponent");
		if (link)
		{
			string groupName;
			link.Get("m_sGroupName", groupName);
			fields.Insert("\"link\":" + Str(groupName));
		}

		if (type && type.IsInherited(SCR_AIGroup))
			fields.Insert(GroupJson(source, aiWorld));

		if (type && type.IsInherited(SCR_EditorRestrictionZoneEntity))
		{
			float warn, zone;
			source.Get("m_fWarnRadius", warn);
			source.Get("m_fZoneRadius", zone);
			fields.Insert(string.Format("\"warn\":%1,\"zone\":%2", Num(warn), Num(zone)));
		}

		return "{" + SCR_StringHelper.Join(",", fields) + "}";
	}

	//------------------------------------------------------------------------------------------------
	//! Members stand where SCR_AIGroup.SpawnGroupMember puts them: formation offset, or index metres along X without one
	protected string GroupJson(IEntitySource source, AIWorld aiWorld)
	{
		array<ResourceName> slots = {};
		source.Get("m_aUnitPrefabSlots", slots);

		string formationName;
		IEntityComponentSource formationSource = SCR_BaseContainerTools.FindComponentSource(source, AIFormationComponent);
		if (formationSource)
			formationSource.Get("DefaultFormation", formationName);

		AIFormationDefinition formation;
		if (aiWorld && formationName != "")
			formation = aiWorld.GetFormation(formationName);

		array<string> offsets = {};
		for (int i, count = slots.Count(); i < count; i++)
		{
			vector offset = Vector(i, 0, 0);
			if (formation)
				offset = formation.GetOffsetPosition(i);

			offsets.Insert(Vec(offset));
		}

		return string.Format("\"slots\":%1,\"formation\":%2,\"formationFound\":%3,\"offsets\":[", slots.Count(), Str(formationName), Bool(formation != null)) + SCR_StringHelper.Join(",", offsets) + "]";
	}

	//------------------------------------------------------------------------------------------------
	//! Only for a simulation that is set up; an empty list means the caller has to work from the bounds
	protected string Wheels(string args)
	{
		array<string> items = {};
		array<string> lines = Lines(args);
		foreach (string line : lines)
		{
			IEntity entity = m_API.SourceToEntity(m_API.GetEntity(m_iSubScene, line.ToInt()));
			array<string> list = {};
			VehicleWheeledSimulation wheels;
			VehicleWheeledSimulation_SA_B wheelsSA;
			if (entity)
			{
				wheels = VehicleWheeledSimulation.Cast(entity.FindComponent(VehicleWheeledSimulation));
				wheelsSA = VehicleWheeledSimulation_SA_B.Cast(entity.FindComponent(VehicleWheeledSimulation_SA_B));
			}

			if (wheels && wheels.IsValid())
			{
				for (int w, count = wheels.WheelCount(); w < count; w++)
				{
					vector position = wheels.WheelGetPosition(w);
					list.Insert(string.Format("[%1,%2,%3,%4]", Num(position[0]), Num(position[1]), Num(position[2]), Num(wheels.WheelGetRadius(w))));
				}
			}
			else if (wheelsSA && wheelsSA.IsValid())
			{
				for (int a, countSA = wheelsSA.WheelCount(); a < countSA; a++)
				{
					vector positionSA = wheelsSA.WheelGetPosition(a);
					list.Insert(string.Format("[%1,%2,%3,%4]", Num(positionSA[0]), Num(positionSA[1]), Num(positionSA[2]), Num(wheelsSA.WheelGetRadius(a))));
				}
			}

			items.Insert("[" + SCR_StringHelper.Join(",", list) + "]");
		}

		return "[" + SCR_StringHelper.Join(",", items) + "]";
	}

	//------------------------------------------------------------------------------------------------
	protected string Surface(string args)
	{
		array<string> items = {};
		array<string> lines = Lines(args);
		foreach (string line : lines)
		{
			array<float> values = Numbers(line);
			if (values.Count() < 2)
				continue;

			float terrain = m_World.GetSurfaceY(values[0], values[1]);
			TraceParam trace = new TraceParam();
			trace.Start = Vector(values[0], terrain + 30, values[1]);
			trace.End = Vector(values[0], terrain - 1, values[1]);
			trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
			float fraction = m_World.TraceMove(trace, Filter);

			float surface = terrain;
			if (fraction < 1)
				surface = trace.Start[1] + (trace.End[1] - trace.Start[1]) * fraction;

			string material = trace.TraceMaterial;
			if (trace.SurfaceProps)
				material += "|" + SCR_BaseContainerTools.GetPrefabResourceName(trace.SurfaceProps);

			items.Insert(string.Format("[%1,%2,%3,%4]", Num(terrain), Num(surface), Str(EntityLabel(trace.TraceEnt)), Str(material)));
		}

		return "[" + SCR_StringHelper.Join(",", items) + "]";
	}

	//------------------------------------------------------------------------------------------------
	protected string Collide(string args)
	{
		array<string> items = {};
		array<string> lines = Lines(args);
		foreach (string line : lines)
		{
			array<float> v = Numbers(line);
			if (v.Count() < 18)
			{
				items.Insert(Str("bad box"));
				continue;
			}

			TraceOBB trace = new TraceOBB();
			trace.Start = Vector(v[0], v[1], v[2]);
			trace.Mat[0] = Vector(v[3], v[4], v[5]);
			trace.Mat[1] = Vector(v[6], v[7], v[8]);
			trace.Mat[2] = Vector(v[9], v[10], v[11]);
			trace.Mins = Vector(v[12], v[13], v[14]);
			trace.Maxs = Vector(v[15], v[16], v[17]);
			trace.Flags = TraceFlags.ENTS;
			trace.LayerMask = EPhysicsLayerPresets.Projectile;

			string label;
			if (m_World.TracePosition(trace, Filter) < 0)
				label = EntityLabel(trace.TraceEnt);

			items.Insert(Str(label));
		}

		return "[" + SCR_StringHelper.Join(",", items) + "]";
	}

	//------------------------------------------------------------------------------------------------
	//! Road generators sit at the start of their spline, so a box query misses them: all scenes are scanned once
	//! per world and the centre lines are cached. The answer keeps only the parts of roads inside the box.
	protected string Roads(string args)
	{
		array<float> v = Numbers(args);
		if (v.Count() < 4)
			return ErrorJson("roads needs minX minZ maxX maxZ");

		LoadRoads();
		array<string> items = {};
		foreach (TMA_PlacementRoad road : s_aRoads)
		{
			if (road.m_vMaxs[0] < v[0] || road.m_vMins[0] > v[2] || road.m_vMaxs[2] < v[1] || road.m_vMins[2] > v[3])
				continue;

			array<vector> part = {};
			foreach (vector point : road.m_aPoints)
			{
				if (point[0] >= v[0] && point[0] <= v[2] && point[2] >= v[1] && point[2] <= v[3])
				{
					part.Insert(point);
					continue;
				}

				if (part.Count() > 1)
					items.Insert(RoadJson(road, part));

				part.Clear();
			}

			if (part.Count() > 1)
				items.Insert(RoadJson(road, part));
		}

		return string.Format("{\"generators\":%1,\"roads\":[", s_aRoads.Count()) + SCR_StringHelper.Join(",", items) + "]}";
	}

	//------------------------------------------------------------------------------------------------
	protected void LoadRoads()
	{
		string worldPath;
		m_API.GetWorldPath(worldPath);
		if (s_aRoads && s_sRoadsWorld == worldPath)
			return;

		s_aRoads = {};
		s_sRoadsWorld = worldPath;
		for (int scene, sceneCount = m_API.GetNumSubScenes(); scene < sceneCount; scene++)
		{
			for (int i, count = m_API.GetEntityCount(scene); i < count; i++)
			{
				IEntitySource source = m_API.GetEntity(scene, i);
				if (!source)
					continue;

				string className = source.GetClassName();
				if (!className.Contains("Road") && !className.Contains("Shape"))
					continue;

				IEntity entity = m_API.SourceToEntity(source);
				RoadGeneratorEntity generator = RoadGeneratorEntity.Cast(entity);
				if (generator)
				{
					AddRoad(generator);
					continue;
				}

				if (!ShapeEntity.Cast(entity))
					continue;

				IEntity child = entity.GetChildren();
				while (child)
				{
					RoadGeneratorEntity childGenerator = RoadGeneratorEntity.Cast(child);
					if (childGenerator)
						AddRoad(childGenerator);

					child = child.GetSibling();
				}
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void AddRoad(RoadGeneratorEntity generator)
	{
		ShapeEntity shape = ShapeEntity.Cast(generator.GetParent());
		if (!shape)
			return;

		foreach (TMA_PlacementRoad known : s_aRoads)
		{
			if (known.m_Generator == generator)
				return;
		}

		array<vector> local = {};
		shape.GenerateTesselatedShape(local);
		if (local.Count() < 2)
			return;

		TMA_PlacementRoad road = new TMA_PlacementRoad();
		road.m_Generator = generator;
		road.m_fWidth = generator.GetRoadWidth();
		road.m_sName = shape.GetName();
		float minX = float.MAX;
		float minZ = float.MAX;
		float maxX = -float.MAX;
		float maxZ = -float.MAX;
		foreach (vector point : local)
		{
			vector world = shape.CoordToParent(point);
			road.m_aPoints.Insert(world);
			minX = Math.Min(minX, world[0]);
			minZ = Math.Min(minZ, world[2]);
			maxX = Math.Max(maxX, world[0]);
			maxZ = Math.Max(maxZ, world[2]);
		}

		road.m_vMins = Vector(minX, 0, minZ);
		road.m_vMaxs = Vector(maxX, 0, maxZ);
		s_aRoads.Insert(road);
	}

	//------------------------------------------------------------------------------------------------
	protected static string RoadJson(TMA_PlacementRoad road, array<vector> points)
	{
		array<string> list = {};
		foreach (vector point : points)
		{
			list.Insert(Vec(point));
		}

		return "{\"name\":" + Str(road.m_sName) + ",\"width\":" + Num(road.m_fWidth) + ",\"points\":[" + SCR_StringHelper.Join(",", list) + "]}";
	}
}

//! Changes the open world in one undoable World Editor action ("TMA placement"); the world is not saved.
//! Lines: "move <id> <x> <y> <z> <row0 3> <row1 3> <row2 3>", "create <prefab> <layerId> <x> <y> <z> <yaw>",
//! "delete <id>". Ids are the ones "entities" reported; deletes run last so the ids stay valid.
class TMA_PlacementApply : TMA_PlacementHandlerBase
{
	//------------------------------------------------------------------------------------------------
	override protected string Run(string command, string args)
	{
		if (command != "apply")
			return ErrorJson("unknown command " + command);

		array<string> errors = {};
		array<int> deletes = {};
		int moved, created;

		array<string> lines = Lines(args);
		m_API.BeginEntityAction("TMA placement");
		foreach (string line : lines)
		{
			array<string> parts = {};
			line.Split(" ", parts, true);
			if (parts.IsEmpty())
				continue;

			if (parts[0] == "move" && parts.Count() >= 14)
			{
				IEntitySource source = m_API.GetEntity(m_iSubScene, parts[1].ToInt());
				if (!source)
				{
					errors.Insert("no entity " + parts[1]);
					continue;
				}

				vector rotation[3];
				rotation[0] = Vector(parts[5].ToFloat(), parts[6].ToFloat(), parts[7].ToFloat());
				rotation[1] = Vector(parts[8].ToFloat(), parts[9].ToFloat(), parts[10].ToFloat());
				rotation[2] = Vector(parts[11].ToFloat(), parts[12].ToFloat(), parts[13].ToFloat());
				vector ypr = Math3D.MatrixToAngles(rotation);

				m_API.SetVariableValue(source, null, "coords", string.Format("%1 %2 %3", parts[2], parts[3], parts[4]));
				m_API.SetVariableValue(source, null, "angles", string.Format("%1 %2 %3", Num(ypr[1]), Num(ypr[0]), Num(ypr[2])));
				moved++;
			}
			else if (parts[0] == "create" && parts.Count() >= 7)
			{
				vector position = Vector(parts[3].ToFloat(), parts[4].ToFloat(), parts[5].ToFloat());
				if (m_API.CreateEntity(parts[1], "", parts[2].ToInt(), null, position, Vector(0, parts[6].ToFloat(), 0)))
					created++;
				else
					errors.Insert("cannot create " + parts[1]);
			}
			else if (parts[0] == "delete" && parts.Count() >= 2)
			{
				deletes.Insert(parts[1].ToInt());
			}
			else
			{
				errors.Insert("bad line: " + line);
			}
		}

		deletes.Sort(true);
		int deleted;
		foreach (int id : deletes)
		{
			IEntitySource deletedSource = m_API.GetEntity(m_iSubScene, id);
			if (deletedSource && m_API.DeleteEntity(deletedSource))
				deleted++;
			else
				errors.Insert(string.Format("cannot delete %1", id));
		}

		m_API.EndEntityAction("TMA placement");

		array<string> quoted = {};
		foreach (string error : errors)
		{
			quoted.Insert(Str(error));
		}

		return string.Format("{\"moved\":%1,\"created\":%2,\"deleted\":%3,\"errors\":[", moved, created, deleted) + SCR_StringHelper.Join(",", quoted) + "]}";
	}
}
#endif
