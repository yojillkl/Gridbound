"""Run inside Unreal Editor's PythonScript commandlet after compiling GridboundEditor."""
import unreal

path = "/Game/Maps/GridArena"
if not unreal.EditorAssetLibrary.does_asset_exist(path):
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not levels.new_level(path):
        raise RuntimeError("Could not create GridArena")
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    start = actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(450, 1500, 100))
    start.set_actor_label("Player Start - Grid (3,10)")
    if not levels.save_current_level():
        raise RuntimeError("Could not save GridArena")
if not unreal.EditorAssetLibrary.does_asset_exist(path):
    raise RuntimeError("GridArena asset not found after creation")
unreal.log("GRIDBOUND_MAP_READY: " + path)
