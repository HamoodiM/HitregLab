"""Hitreg Lab content: weapon + aim assist data assets and the three maps (run in the editor).

Re-running overwrites the weapon values with these presets, so tune in the editor and copy numbers back here
if you want them to survive. Maps are just a level builder actor (geometry/lights are built in code).
"""
import unreal

WEAPONS = "/Game/HitregLab/Weapons"
AIM = "/Game/HitregLab/Aim"
MAPS = "/Game/HitregLab/Maps"

PRESETS = {
    "DA_Rifle": dict(display_name="Rifle", fire_mode=unreal.HLFireMode.AUTO, rounds_per_minute=600, magazine_size=30, reload_time=1.8,
                     damage=25, falloff_start=2000, falloff_end=5000, min_damage_scale=0.6,
                     spread_base=0.4, spread_per_shot=0.25, spread_max=3.0, spread_recovery=8.0,
                     recoil_pattern=[(0.55, 0.0), (0.6, 0.1), (0.6, 0.15), (0.55, -0.1), (0.5, -0.25), (0.5, -0.2), (0.45, 0.2), (0.45, 0.3)],
                     color=(0.15, 0.16, 0.18)),
    "DA_SMG": dict(display_name="SMG", fire_mode=unreal.HLFireMode.AUTO, rounds_per_minute=900, magazine_size=32, reload_time=1.6,
                   damage=18, falloff_start=1200, falloff_end=3000, min_damage_scale=0.6,
                   spread_base=0.8, spread_per_shot=0.18, spread_max=3.5, spread_recovery=10.0,
                   recoil_pattern=[(0.35, 0.05), (0.4, -0.1), (0.4, 0.15), (0.35, -0.2), (0.3, 0.2)],
                   color=(0.12, 0.2, 0.28)),
    "DA_Marksman": dict(display_name="Marksman", fire_mode=unreal.HLFireMode.SEMI, rounds_per_minute=240, magazine_size=10, reload_time=2.2,
                        damage=55, headshot_multiplier=1.25, falloff_start=6000, falloff_end=9000, min_damage_scale=0.8,
                        spread_base=0.15, spread_per_shot=0.9, spread_max=2.5, spread_recovery=6.0,
                        recoil_pattern=[(2.2, 0.3)], ads_fov=40, aim_assist_scale=0.5, color=(0.25, 0.18, 0.12)),
    "DA_Launcher": dict(display_name="Launcher", fire_mode=unreal.HLFireMode.SEMI, rounds_per_minute=70, magazine_size=4, reload_time=2.6,
                        damage=90, b_projectile=True, projectile_speed=4000, projectile_gravity_scale=0.6, splash_radius=250,
                        spread_base=0.0, spread_moving=0.0, recoil_pattern=[(3.5, 0.0)], aim_assist_scale=0.0,
                        color=(0.3, 0.12, 0.1)),
}

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary


def get_or_create(name, path, cls):
    full = f"{path}/{name}"
    if eal.does_asset_exist(full):
        return unreal.load_asset(full)
    return asset_tools.create_asset(name, path, cls, unreal.DataAssetFactory())


def make_weapons():
    for name, props in PRESETS.items():
        a = get_or_create(name, WEAPONS, unreal.HLWeaponDefinition)
        for key, value in props.items():
            if key == "display_name":
                value = unreal.Text(value)
            elif key == "recoil_pattern":
                value = [unreal.Vector2D(p, y) for p, y in value]
            elif key == "color":
                value = unreal.LinearColor(value[0], value[1], value[2], 1.0)
            a.set_editor_property(key, value)
        eal.save_loaded_asset(a)
    print("weapons ok")


def make_aim():
    a = get_or_create("DA_AimAssist", AIM, unreal.HLAimAssistSettings)
    eal.save_loaded_asset(a)
    print("aim ok")


def make_map(name, layout):
    path = f"{MAPS}/{name}"
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if eal.does_asset_exist(path):
        les.load_level(path)
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.HLLevelBuilder):
            actor.set_editor_property("layout", layout)
            actor.rerun_construction_scripts()
    else:
        les.new_level(path)
        actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        builder = actor_sub.spawn_actor_from_class(unreal.HLLevelBuilder, unreal.Vector(0, 0, 0))
        builder.set_editor_property("layout", layout)
        builder.rerun_construction_scripts()
    les.save_current_level()
    print(f"{name} ok")


def make_all():
    make_weapons()
    make_aim()
    make_map("Lvl_Range", unreal.HLLayout.RANGE)
    make_map("Lvl_Peek", unreal.HLLayout.PEEK)
    make_map("Lvl_Arena", unreal.HLLayout.ARENA)
