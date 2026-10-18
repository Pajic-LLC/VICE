from pathlib import Path


def once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)

path = Path("tests/editor_project_build_tests.cpp")
text = path.read_text()
text = once(
    text,
    '    const auto game_source = project_root / "game" / "game.cpp";\n',
    '    const auto game_source = project_root / "game" / "game.cpp";\n'
    '    const auto texture_source = project_root / "assets" / "textures" / "surface.vtex";\n'
    '    const auto texture_image = project_root / "assets" / "textures" / "surface.ppm";\n',
    "texture paths",
)
text = once(
    text,
    '''    write_text(
        startup_world,
        "vice-world-source 1\\n\\n"
        "region 1 1 0 0 0 0 0 0 full bounds -10 -10 -10 10 10 10\\n"
    );
''',
    '''    write_text(
        startup_world,
        "vice-world-source 1\\n\\n"
        "region 1 1 0 0 0 0 0 0 full bounds -10 -10 -10 10 10 10\\n"
    );
    write_text(
        texture_image,
        "P3\\n2 1\\n255\\n255 32 16  16 96 255\\n"
    );
    write_text(
        texture_source,
        "vice-texture-source 1\\n"
        "id 71000000-0000-4000-8000-000000000133\\n"
        "image \\\"textures/surface.ppm\\\"\\n"
        "color_space srgb\\n"
    );
''',
    "texture fixture",
)
text = once(
    text,
    '    require(built.game_module.empty(), "legacy project without game source has no module");\n',
    '    require(built.game_module.empty(), "legacy project without game source has no module");\n'
    '    require(built.texture_count == 1U, "project build cooks one texture");\n',
    "texture count",
)
text = once(
    text,
    '''    const auto runtime_world = world::load_world_snapshot(expected_runtime_world);
    require(runtime_world.snapshot.has_value(), "runtime startup World loads");
''',
    '''    const auto runtime_world = world::load_world_snapshot(expected_runtime_world);
    require(runtime_world.snapshot.has_value(), "runtime startup World loads");

    const auto expected_runtime_texture = (
        expected_output /
        "content" /
        "43000000-0000-4000-8000-000000000133" /
        "textures" /
        "surface.vtexc"
    ).lexically_normal();
    require(game_build.texture_count == 1U, "game build reports one cooked texture");
    require(std::filesystem::is_regular_file(expected_runtime_texture), "runtime texture is staged");
''',
    "staged texture assertion",
)
path.write_text(text)
