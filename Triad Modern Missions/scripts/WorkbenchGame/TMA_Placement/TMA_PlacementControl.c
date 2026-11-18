#ifdef WORKBENCH
//! NET API for Tools/Placement: which world World Editor has open, save it, open another one, fly to a place.
//! Commands (request as in TMA_PlacementWorld.c): "world"; "save"; "open" with the resource in args,
//! e.g. "{FE44492E52E75C74}Worlds/Gogland/BlackBox_Ep3.ent"; "goto" with "<x> <z> [height] [distance]" —
//! in edit mode it moves the editor camera above that spot (looking north from `distance` to the south),
//! in play mode it teleports the local player there. The answer is a short JSON text.
class TMA_PlacementControl : NetApiHandler
{
	//------------------------------------------------------------------------------------------------
	override JsonApiStruct GetRequest()
	{
		return new TMA_PlacementRequest();
	}

	//------------------------------------------------------------------------------------------------
	override JsonApiStruct GetResponse(JsonApiStruct request)
	{
		TMA_PlacementResponse response = new TMA_PlacementResponse();
		response.json = Run(TMA_PlacementRequest.Cast(request));
		response.total = response.json.Length();
		return response;
	}

	//------------------------------------------------------------------------------------------------
	protected string Run(TMA_PlacementRequest req)
	{
		if (!req)
			return "{\"error\":\"bad request\"}";

		WorldEditor worldEditor = Workbench.GetModule(WorldEditor);
		if (!worldEditor)
			return "{\"error\":\"no World Editor module\"}";

		// Opening or saving a world while the game runs in World Editor loads a second world over the running one:
		// 10.11 it gave "This is not the only AIWorld" and a NULL map entity every frame in the user's play test.
		WorldEditorAPI api = worldEditor.GetApi();
		if (req.command != "world" && req.command != "goto" && api && api.IsGameMode())
			return "{\"error\":\"the game is running in World Editor (play mode): stop it first\"}";

		if (req.command == "goto")
			return Goto(api, req.args);

		if (req.command == "save")
		{
			bool saved = worldEditor.Save();
			return string.Format("{\"saved\":%1,\"world\":\"%2\"}", Flag(saved), WorldPath(worldEditor));
		}

		if (req.command == "open")
		{
			bool opened = worldEditor.SetOpenedResource(req.args);
			return string.Format("{\"opened\":%1,\"world\":\"%2\"}", Flag(opened), WorldPath(worldEditor));
		}

		if (req.command == "world")
			return string.Format("{\"world\":\"%1\"}", WorldPath(worldEditor));

		return "{\"error\":\"unknown command\"}";
	}

	//------------------------------------------------------------------------------------------------
	//! "<x> <z> [height] [distance]": the editor camera goes `height` above the ground and `distance` to the
	//! south of the spot and looks at it; with the game running the local player is teleported onto the spot.
	protected static string Goto(WorldEditorAPI api, string args)
	{
		if (!api)
			return "{\"error\":\"no World Editor API\"}";

		array<string> parts = {};
		args.Split(" ", parts, true);
		if (parts.Count() < 2)
			return "{\"error\":\"goto needs <x> <z> [height] [distance]\"}";

		float x = parts[0].ToFloat();
		float z = parts[1].ToFloat();
		float height = 120;
		if (parts.Count() > 2)
			height = parts[2].ToFloat();

		float distance = 250;
		if (parts.Count() > 3)
			distance = parts[3].ToFloat();

		BaseWorld world = api.GetWorld();
		if (!world)
			return "{\"error\":\"no world\"}";

		float ground = world.GetSurfaceY(x, z);
		vector spot = Vector(x, ground, z);

		if (api.IsGameMode())
		{
			int playerId = SCR_PlayerController.GetLocalPlayerId();
			if (playerId <= 0)
				return "{\"error\":\"no local player to teleport\"}";

			if (!SCR_Global.TeleportPlayer(playerId, spot))
				return "{\"error\":\"teleport refused (outside the terrain?)\"}";

			return string.Format("{\"teleported\":true,\"x\":%1,\"y\":%2,\"z\":%3}", x, ground, z);
		}

		vector camera = Vector(x, ground + height, z - distance);
		api.SetCamera(camera, spot - camera);
		return string.Format("{\"camera\":true,\"x\":%1,\"y\":%2,\"z\":%3}", x, ground, z);
	}

	//------------------------------------------------------------------------------------------------
	protected static string Flag(bool value)
	{
		if (value)
			return "true";

		return "false";
	}

	//------------------------------------------------------------------------------------------------
	protected static string WorldPath(WorldEditor worldEditor)
	{
		WorldEditorAPI api = worldEditor.GetApi();
		string path;
		if (api)
			api.GetWorldPath(path);

		return path;
	}
}
#endif
