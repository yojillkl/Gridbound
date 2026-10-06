import unreal

lib = unreal.MaterialEditingLibrary
assets = unreal.AssetToolsHelpers.get_asset_tools()
folder = '/Game/Art/Materials'

def make(name, roughness, metallic=0.0, water=False, magic=False):
    path = folder + '/' + name
    mat = unreal.load_asset(path)
    if not mat:
        mat = assets.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    lib.delete_all_material_expressions(mat)
    mat.set_editor_property('two_sided', True)
    vertex = lib.create_material_expression(mat, unreal.MaterialExpressionVertexColor, -600, 0)
    world = lib.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -600, 180)
    time = lib.create_material_expression(mat, unreal.MaterialExpressionTime, -600, 300)
    custom = lib.create_material_expression(mat, unreal.MaterialExpressionCustom, -200, 0)
    custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    inputs = []
    for name in ['Tint', 'P', 'T']:
        item = unreal.CustomInput()
        item.set_editor_property('input_name', name)
        inputs.append(item)
    custom.set_editor_property('inputs', inputs)
    assert lib.connect_material_expressions(vertex, '', custom, 'Tint'), 'Vertex tint connection failed'
    assert lib.connect_material_expressions(world, '', custom, 'P'), 'World position connection failed'
    assert lib.connect_material_expressions(time, '', custom, 'T'), 'Time connection failed'
    if magic:
        mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
        custom.set_editor_property('code', 'return Tint * 1.7;')
        lib.connect_material_property(custom, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    else:
        mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
        code = '''float2 q=(P.xy+P.z*float2(.31,.67))*.025;
float2 i=floor(q), f=frac(q); f=f*f*(3.0-2.0*f);
float4 h=frac(sin(float4(dot(i,float2(127.1,311.7)),dot(i+float2(1,0),float2(127.1,311.7)),dot(i+float2(0,1),float2(127.1,311.7)),dot(i+1,float2(127.1,311.7))))*43758.5453);
float patch=lerp(lerp(h.x,h.y,f.x),lerp(h.z,h.w,f.x),f.y);
float grain=frac(sin(dot(floor(P.xy*2+P.z*.17),float2(12.9898,78.233)))*43758.5453);
return max(0.0,Tint*(.88+.17*patch+.035*grain));'''
        if water:
            code = '''float wave=sin(P.x*.025+P.y*.016-T*.8)*sin(P.y*.041+T*.5);
float ripple=pow(saturate(sin(P.y*.075-P.x*.013+sin(P.x*.03)*1.8-T*1.3)),24.0);
float broken=saturate(sin(P.x*.027+sin(P.y*.018)*2.0));
return Tint*(.9+.07*wave)+float3(.018,.035,.031)*ripple*broken;'''
        custom.set_editor_property('code', code)
        lib.connect_material_property(custom, '', unreal.MaterialProperty.MP_BASE_COLOR)
        for value, prop, y in [(roughness, unreal.MaterialProperty.MP_ROUGHNESS, 200), (metallic, unreal.MaterialProperty.MP_METALLIC, 300)]:
            scalar = lib.create_material_expression(mat, unreal.MaterialExpressionConstant, 0, y)
            scalar.set_editor_property('r', value)
            lib.connect_material_property(scalar, '', prop)
    lib.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    unreal.log('FANTASY_MATERIAL_READY ' + path)

make('M_FantasyPaint', .82)
make('M_FantasyMetal', .64, .38)
make('M_FantasyCloth', .95)
make('M_FantasyWater', .46, .05, water=True)
make('M_FantasyMagic', .3, magic=True)
unreal.log('FANTASY_MATERIALS_COMPLETE')
