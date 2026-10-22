from pathlib import Path

source_path = Path("tools/shader_cooker/src/shader_cooker.cpp")
source = source_path.read_text()
old = '''        const auto fields = split_whitespace(content);
        if (fields.size() < 7U) {
            reflection.diagnostics = "DXC emitted a malformed resource binding row: " + content;
            return reflection;
        }

        const std::string& binding_token = fields[fields.size() - 2U];
        const std::string& count_token = fields.back();
        const auto location = parse_binding_location(binding_token);
'''
new = '''        const auto fields = split_whitespace(content);
        if (fields.size() < 6U) {
            reflection.diagnostics = "DXC emitted a malformed resource binding row: " + content;
            return reflection;
        }

        std::string binding_token;
        std::string count_token;
        constexpr std::string_view compact_unbounded_suffix{"unbounded"};
        const std::string& final_token = fields.back();
        if (final_token.size() > compact_unbounded_suffix.size() &&
            final_token.ends_with(compact_unbounded_suffix)) {
            binding_token = final_token.substr(
                0,
                final_token.size() - compact_unbounded_suffix.size()
            );
            count_token = "unbounded";
        } else {
            if (fields.size() < 7U) {
                reflection.diagnostics = "DXC emitted a malformed resource binding row: " + content;
                return reflection;
            }
            binding_token = fields[fields.size() - 2U];
            count_token = final_token;
        }

        const auto location = parse_binding_location(binding_token);
'''
count = source.count(old)
if count != 1:
    raise SystemExit(f"expected one shader-cooker parser block, found {count}")
source_path.write_text(source.replace(old, new))

test_path = Path("tests/shader_cooker_tests.cpp")
test = test_path.read_text()
marker = '''    CHECK(unordered_access.count == 0U);
    CHECK(unordered_access.unbounded);

'''
addition = '''    CHECK(unordered_access.count == 0U);
    CHECK(unordered_access.unbounded);

    constexpr const char* compact_unbounded_assembly = R"DXC(
;
; Resource Bindings:
;
; Name                                 Type  Format         Dim      ID      HLSL Bind  Count
; ------------------------------ ---------- ------- ----------- ------- -------------- ------
; InputBuffers                      texture  struct         r/o      T0     t0unbounded
;
; ViewId state:
)DXC";
    const ShaderReflection compact_unbounded_reflection =
        parse_dxc_resource_bindings(compact_unbounded_assembly);
    CHECK(compact_unbounded_reflection.valid);
    CHECK(compact_unbounded_reflection.diagnostics.empty());
    CHECK(compact_unbounded_reflection.resources.size() == 1U);
    const auto& compact_unbounded = compact_unbounded_reflection.resources[0];
    CHECK(compact_unbounded.name == "InputBuffers");
    CHECK(compact_unbounded.register_class == ShaderRegisterClass::shader_resource);
    CHECK(compact_unbounded.binding == 0U);
    CHECK(compact_unbounded.space == 0U);
    CHECK(compact_unbounded.count == 0U);
    CHECK(compact_unbounded.unbounded);

    constexpr const char* malformed_compact_unbounded_assembly = R"DXC(
;
; Resource Bindings:
;
; Name                                 Type  Format         Dim      ID      HLSL Bind  Count
; ------------------------------ ---------- ------- ----------- ------- -------------- ------
; InputBuffers                      texture  struct         r/o      T0       unbounded
;
)DXC";
    const ShaderReflection malformed_compact_unbounded_reflection =
        parse_dxc_resource_bindings(malformed_compact_unbounded_assembly);
    CHECK(!malformed_compact_unbounded_reflection.valid);
    CHECK(!malformed_compact_unbounded_reflection.diagnostics.empty());

'''
count = test.count(marker)
if count != 1:
    raise SystemExit(f"expected one shader-cooker test insertion point, found {count}")
test_path.write_text(test.replace(marker, addition))
