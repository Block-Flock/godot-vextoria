def can_build(env, platform):
    env.module_add_dependencies("vextoria_terrain", ["csg"])
    return not env["disable_3d"]

def configure(env):
    pass
