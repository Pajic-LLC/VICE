#include <bit>
#include <fstream>
#include <iomanip>
#include <locale>
#include <vice/tools/presentation_cooker.hpp>
namespace vice::tools {
namespace {
bool fail(std::string *d, const char *s) {
    if (d)
        *d = s;
    return false;
}
} // namespace
std::optional<presentation::Sound> import_wav(const std::filesystem::path &path,
                                              world::ResourceId id, std::string *d) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size < 44 || size > 16 * 1024 * 1024) {
        fail(d, "WAV missing or exceeds 16 MiB.");
        return std::nullopt;
    }
    std::ifstream in(path, std::ios::binary);
    std::vector<unsigned char> b(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char *>(b.data()), static_cast<std::streamsize>(b.size()));
    const auto u16 = [&](std::size_t o) {
        return static_cast<unsigned>(b[o]) | (static_cast<unsigned>(b[o + 1]) << 8);
    };
    const auto u32 = [&](std::size_t o) {
        std::uint32_t v = 0;
        for (unsigned i = 0; i < 4; ++i)
            v |= static_cast<std::uint32_t>(b[o + i]) << (i * 8);
        return v;
    };
    if (!in || std::string(reinterpret_cast<char *>(b.data()), 4) != "RIFF" ||
        std::string(reinterpret_cast<char *>(b.data() + 8), 4) != "WAVE" ||
        u32(4) != b.size() - 8) {
        fail(d, "Invalid RIFF/WAVE header.");
        return std::nullopt;
    }
    unsigned format = 0, bits = 0;
    std::size_t offset = 0, length = 0;
    bool got_format = false, got_data = false;
    for (std::size_t p = 12; p < b.size();) {
        if (b.size() - p < 8) {
            fail(d, "Truncated WAV chunk.");
            return std::nullopt;
        }
        const std::string name(reinterpret_cast<char *>(b.data() + p), 4);
        const auto count = u32(p + 4);
        p += 8;
        if (count > b.size() - p) {
            fail(d, "Invalid WAV chunk length.");
            return std::nullopt;
        }
        if (name == "fmt ") {
            if (got_format || count < 16) {
                fail(d, "Invalid duplicate WAV format.");
                return std::nullopt;
            }
            got_format = true;
            format = u16(p);
            bits = u16(p + 14);
            if (u16(p + 2) != 1 || u32(p + 4) != 48000 ||
                !((format == 1 && bits == 16) || (format == 3 && bits == 32)) ||
                u16(p + 12) != bits / 8 || u32(p + 8) != 48000 * (bits / 8)) {
                fail(d, "Sound import requires mono 48 kHz PCM16 or float32 WAV.");
                return std::nullopt;
            }
        } else if (name == "data") {
            if (got_data) {
                fail(d, "Duplicate WAV data.");
                return std::nullopt;
            }
            got_data = true;
            offset = p;
            length = count;
        }
        p += count;
        if (count % 2) {
            if (p >= b.size()) {
                fail(d, "Missing WAV padding.");
                return std::nullopt;
            }
            ++p;
        }
    }
    if (!got_data || !got_format || length == 0 || length % (bits / 8) ||
        length / (bits / 8) > 4000000) {
        fail(d, "Missing or invalid WAV samples.");
        return std::nullopt;
    }
    presentation::Sound sound;
    sound.id = id;
    sound.samples.resize(length / (bits / 8));
    for (std::size_t i = 0; i < sound.samples.size(); ++i) {
        const auto p = offset + i * (bits / 8);
        float v = 0;
        if (format == 1) {
            const auto word = static_cast<std::uint16_t>(u16(p));
            v = static_cast<float>(std::bit_cast<std::int16_t>(word)) / 32768;
        } else
            v = std::bit_cast<float>(u32(p));
        if (!std::isfinite(v) || std::abs(v) > 1) {
            fail(d, "Invalid WAV sample amplitude.");
            return std::nullopt;
        }
        sound.samples[i] = v;
    }
    return sound;
}
PresentationCookResult cook_project_presentation(const project::Workspace &w) {
    PresentationCookResult result;
    std::error_code ec;
    std::filesystem::recursive_directory_iterator it(
        w.asset_root, std::filesystem::directory_options::skip_permission_denied, ec),
        end;
    for (; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file() || it->is_symlink())
            continue;
        const auto ext = it->path().extension();
        if (ext != ".vrig" && ext != ".vsound")
            continue;
        auto relative = std::filesystem::relative(it->path(), w.asset_root, ec);
        if (ec)
            break;
        auto destination = w.cooked_root / relative;
        std::filesystem::create_directories(destination.parent_path(), ec);
        if (ec)
            break;
        if (ext == ".vrig") {
            const auto rig = presentation::read_rig(it->path(), &result.diagnostics);
            if (!rig)
                return result;
            destination.replace_extension(".vrigc");
            std::ofstream out(destination, std::ios::binary);
            out << presentation::serialize_rig(*rig);
            if (!out) {
                result.diagnostics = "Could not write cooked rig.";
                return result;
            }
            ++result.rigs;
        } else {
            std::ifstream in(it->path());
            in.imbue(std::locale::classic());
            std::string header, id, file, extra;
            unsigned version = 0;
            if (!(in >> header >> version >> id >> std::quoted(file)) || header != "vice-sound" ||
                version != 1 || (in >> extra)) {
                result.diagnostics = "Invalid sound source manifest.";
                return result;
            }
            const auto parsed = world::PersistentId::parse(id);
            if (!parsed) {
                result.diagnostics = "Invalid sound UUID.";
                return result;
            }
            const auto source =
                std::filesystem::weakly_canonical(it->path().parent_path() / file, ec);
            const auto root = std::filesystem::weakly_canonical(w.asset_root, ec);
            const auto path = source.lexically_relative(root);
            if (ec || path.empty() || path.is_absolute() || *path.begin() == "..") {
                result.diagnostics = "Sound source must remain inside project assets.";
                return result;
            }
            const auto sound = import_wav(source, {*parsed}, &result.diagnostics);
            if (!sound)
                return result;
            destination.replace_extension(".vpcmc");
            if (!presentation::write_pcm(destination, *sound, &result.diagnostics))
                return result;
            ++result.sounds;
        }
    }
    if (ec) {
        result.diagnostics = "Could not enumerate/write presentation assets: " + ec.message();
        return result;
    }
    result.success = true;
    return result;
}
} // namespace vice::tools
