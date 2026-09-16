"""Create the reusable material for the original low-poly terrain and characters."""
import unreal

folder = '/Game/Art/Materials'
unreal.EditorAssetLibrary.make_directory(folder)
path = folder + '/M_FacetedTile'
material = unreal.EditorAssetLibrary.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
if material is None:
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_FacetedTile', folder, unreal.Material, unreal.MaterialFactoryNew())
material.set_editor_property('two_sided', True)
edit = unreal.MaterialEditingLibrary
edit.delete_all_material_expressions(material)
color = edit.create_material_expression(material, unreal.MaterialExpressionVertexColor, -500, 0)
assert edit.connect_material_property(color, '', unreal.MaterialProperty.MP_BASE_COLOR)
ambient = edit.create_material_expression(material, unreal.MaterialExpressionMultiply, -220, 180)
ambient.set_editor_property('const_b', 0.22)
assert edit.connect_material_expressions(color, '', ambient, 'A')
assert edit.connect_material_property(ambient, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
rough = edit.create_material_expression(material, unreal.MaterialExpressionConstant, -200, 350)
rough.set_editor_property('r', 0.95)
assert edit.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
spec = edit.create_material_expression(material, unreal.MaterialExpressionConstant, -200, 430)
spec.set_editor_property('r', 0.12)
assert edit.connect_material_property(spec, '', unreal.MaterialProperty.MP_SPECULAR)
edit.recompile_material(material)
if not unreal.EditorAssetLibrary.save_loaded_asset(material):
    raise RuntimeError('Material save failed')
unreal.log('GRIDBOUND_MATERIAL_READY: ' + path)

sky_path = folder + '/M_SkyBackdrop'
sky = unreal.EditorAssetLibrary.load_asset(sky_path) if unreal.EditorAssetLibrary.does_asset_exist(sky_path) else None
if sky is None:
    sky = unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_SkyBackdrop', folder, unreal.Material, unreal.MaterialFactoryNew())
sky.set_editor_property('two_sided', True)
sky.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
edit.delete_all_material_expressions(sky)
sky_color = edit.create_material_expression(sky, unreal.MaterialExpressionConstant3Vector, -200, 0)
sky_color.set_editor_property('constant', unreal.LinearColor(0.22, 0.43, 0.58, 1.0))
assert edit.connect_material_property(sky_color, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
edit.recompile_material(sky)
assert unreal.EditorAssetLibrary.save_loaded_asset(sky)
unreal.log('GRIDBOUND_SKY_READY')
