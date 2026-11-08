"""
One-shot editor setup for TechnoViz: creates the materials and the L_Reactor level.

Run inside the editor (Output Log, Python):   py techno_setup.py
or headless:  UnrealEditor-Cmd.exe TechnoViz.uproject -run=pythonscript -script="techno_setup.py"

Safe to re-run: existing materials are rebuilt in place.
"""
import unreal

MAT_DIR = "/Game/TechnoViz/Materials"
MAP_PATH = "/Game/TechnoViz/Maps/L_Reactor"

AT = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary


def log(msg):
    unreal.log("[TechnoViz setup] " + msg)


def get_or_create_material(name):
    path = f"{MAT_DIR}/{name}"
    if EAL.does_asset_exist(path):
        mat = EAL.load_asset(path)
        MEL.delete_all_material_expressions(mat)
        return mat
    return AT.create_asset(name, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())


def expr(mat, cls, x, y, **props):
    e = MEL.create_material_expression(mat, cls, x, y)
    for k, v in props.items():
        e.set_editor_property(k, v)
    return e


def vec_param(mat, name, color, x, y):
    return expr(mat, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name, default_value=unreal.LinearColor(*color))


def scalar_param(mat, name, value, x, y):
    return expr(mat, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=value)


def const(mat, v, x, y):
    return expr(mat, unreal.MaterialExpressionConstant, x, y, r=v)


def mul(mat, a, b, x, y):
    m = expr(mat, unreal.MaterialExpressionMultiply, x, y)
    MEL.connect_material_expressions(a, "", m, "A")
    MEL.connect_material_expressions(b, "", m, "B")
    return m


def custom(mat, code, inputs, x, y, out_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1):
    c = expr(mat, unreal.MaterialExpressionCustom, x, y, code=code, output_type=out_type)
    ins = []
    for name in inputs:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", name)
        ins.append(ci)
    c.set_editor_property("inputs", ins)
    return c


def finish(mat, name):
    MEL.layout_material_expressions(mat)
    MEL.recompile_material(mat)
    EAL.save_asset(f"{MAT_DIR}/{name}")
    log("built " + name)


def emissive_chain(mat):
    """Color * Intensity * PerInstanceCustomData[0] (defaults to 1 on non-instanced meshes)."""
    color = vec_param(mat, "Color", (1, 0.05, 0.02, 1), -900, 0)
    inten = scalar_param(mat, "Intensity", 10.0, -900, 200)
    pic = expr(mat, unreal.MaterialExpressionPerInstanceCustomData, -900, 350, data_index=0, const_default_value=1.0)
    m1 = mul(mat, color, inten, -600, 100)
    return mul(mat, m1, pic, -350, 150)


def build_emissive():
    name = "M_TV_Emissive"
    mat = get_or_create_material(name)
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    mat.set_editor_property("used_with_niagara_mesh_particles", True)
    em = emissive_chain(mat)
    MEL.connect_material_property(em, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(expr(mat, unreal.MaterialExpressionConstant3Vector, -350, -200, constant=unreal.LinearColor(0.02, 0.02, 0.02, 1)), "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(const(mat, 0.6, -350, -100), "", unreal.MaterialProperty.MP_METALLIC)
    MEL.connect_material_property(const(mat, 0.25, -350, -50), "", unreal.MaterialProperty.MP_ROUGHNESS)
    finish(mat, name)


def build_laser():
    name = "M_TV_Laser"
    mat = get_or_create_material(name)
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("two_sided", True)
    em = emissive_chain(mat)
    # Soften the beam edges with a fresnel-ish falloff so the cylinders read as beams of light.
    fres = expr(mat, unreal.MaterialExpressionFresnel, -350, 350, exponent=2.0)
    one_minus = expr(mat, unreal.MaterialExpressionOneMinus, -200, 350)
    MEL.connect_material_expressions(fres, "", one_minus, "")
    MEL.connect_material_property(mul(mat, em, one_minus, -100, 200), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(mat, name)


def build_floor():
    name = "M_TV_Floor"
    mat = get_or_create_material(name)
    wp = expr(mat, unreal.MaterialExpressionWorldPosition, -1100, 0)
    # Wet concrete: tiled roughness variation (puddles) + fine grid lines that glow faintly.
    rough = custom(mat,
                   "float2 c = floor(WP.xy / 80.0);"
                   "float n = frac(sin(dot(c, float2(12.9898, 78.233))) * 43758.5453);"
                   "float2 c2 = floor(WP.xy / 330.0);"
                   "float n2 = frac(sin(dot(c2, float2(39.346, 11.135))) * 24634.6345);"
                   "return lerp(0.04, 0.32, saturate(n * 0.4 + n2 * 0.8 - 0.2));",
                   ["WP"], -700, 0)
    MEL.connect_material_expressions(wp, "", rough, "WP")
    grid = custom(mat,
                  "float2 g = abs(frac(WP.xy / 400.0) - 0.5);"
                  "return smoothstep(0.488, 0.5, max(g.x, g.y));",
                  ["WP"], -700, 250)
    MEL.connect_material_expressions(wp, "", grid, "WP")
    gcol = vec_param(mat, "GridColor", (0.3, 0.02, 0.02, 1), -700, 400)
    gint = scalar_param(mat, "GridIntensity", 2.0, -700, 550)
    em = mul(mat, mul(mat, grid, gcol, -450, 300), gint, -250, 350)
    MEL.connect_material_property(em, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.connect_material_property(expr(mat, unreal.MaterialExpressionConstant3Vector, -350, -250, constant=unreal.LinearColor(0.012, 0.012, 0.014, 1)), "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(const(mat, 0.85, -350, -150), "", unreal.MaterialProperty.MP_METALLIC)
    finish(mat, name)


def build_metal():
    name = "M_TV_Metal"
    mat = get_or_create_material(name)
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    MEL.connect_material_property(expr(mat, unreal.MaterialExpressionConstant3Vector, -350, -200, constant=unreal.LinearColor(0.05, 0.05, 0.055, 1)), "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(const(mat, 1.0, -350, -100), "", unreal.MaterialProperty.MP_METALLIC)
    MEL.connect_material_property(const(mat, 0.32, -350, 0), "", unreal.MaterialProperty.MP_ROUGHNESS)
    finish(mat, name)


def build_level():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if EAL.does_asset_exist(MAP_PATH):
        les.load_level(MAP_PATH)
    else:
        les.new_level(MAP_PATH)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not [a for a in eas.get_all_level_actors() if isinstance(a, unreal.TechnoStageDirector)]:
        eas.spawn_actor_from_class(unreal.TechnoStageDirector, unreal.Vector(0, 0, 0))
    les.save_current_level()
    log("level ready: " + MAP_PATH)


def main():
    for d in (MAT_DIR, "/Game/TechnoViz/Maps", "/Game/TechnoViz/FX"):
        if not EAL.does_directory_exist(d):
            EAL.make_directory(d)
    build_emissive()
    build_laser()
    build_floor()
    build_metal()
    build_level()
    log("done")


main()
