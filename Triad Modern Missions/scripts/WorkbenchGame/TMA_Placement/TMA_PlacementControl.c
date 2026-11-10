#ifdef WORKBENCH
//! NET API for Tools/Placement: which world World Editor has open, save it, open another one.
//! Commands (request as in TMA_PlacementWorld.c): "world"; "save"; "open" with the resource in args,
//! e.g. "{FE44492E52E75C74}Worlds/Gogland/BlackBox_Ep3.ent". The answer is a short JSON text.
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
		if (req.command != "world" && api && api.IsGameMode())
			return "{\"error\":\"the game is running in World Editor (play mode): stop it first\"}";

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
