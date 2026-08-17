#include "sa3_backend.h"
#include "libsa3.h"
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>
#include <vector>
static std::string U(const std::wstring &s)
{
    if (s.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), 0, 0, 0, 0);
    std::string o(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), o.data(), n, 0, 0);
    return o;
}
static std::wstring W(const std::string &s)
{
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), 0, 0);
    std::wstring o(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), o.data(), n);
    return o;
}
template <class T> T S(HMODULE m, const char *n)
{
    return reinterpret_cast<T>(GetProcAddress(m, n));
}
struct Api
{
    HMODULE d{};
    decltype(&sa3_init_ex) i{};
    decltype(&sa3_generate_ex) g{};
    decltype(&sa3_free_audio) fa{};
    decltype(&sa3_free) fc{};
    bool load(const std::filesystem::path &p, std::wstring &e)
    {
        static std::mutex moduleMutex;
        static std::map<std::wstring, HMODULE> modules;
        {
            std::lock_guard<std::mutex> lock(moduleMutex);
            const std::wstring key = p.lexically_normal().wstring();
            const auto existing = modules.find(key);
            if (existing != modules.end())
                d = existing->second;
            else
            {
                d = LoadLibraryExW(p.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
                if (d)
                    modules.emplace(key, d);
            }
        }
        if (!d)
        {
            e = L"Cannot load backend: " + p.wstring() + L" (error " + std::to_wstring(GetLastError()) + L")";
            return false;
        }
        i = S<decltype(i)>(d, "sa3_init_ex");
        g = S<decltype(g)>(d, "sa3_generate_ex");
        fa = S<decltype(fa)>(d, "sa3_free_audio");
        fc = S<decltype(fc)>(d, "sa3_free");
        return i && g && fa && fc;
    }
};
static bool wav(const std::filesystem::path &p, const sa3_audio &a, std::wstring &e)
{
    std::ofstream f(p, std::ios::binary);
    if (!f)
    {
        e = L"Cannot create output WAV";
        return false;
    }
    uint16_t tag = 1, ch = (uint16_t)a.n_ch, bits = 16, align = ch * 2;
    uint32_t rate = a.sample_rate, br = rate * align, data = a.n_samp * align, riff = 36 + data, fmt = 16;
    f.write("RIFF", 4);
    f.write((char *)&riff, 4);
    f.write("WAVEfmt ", 8);
    f.write((char *)&fmt, 4);
    f.write((char *)&tag, 2);
    f.write((char *)&ch, 2);
    f.write((char *)&rate, 4);
    f.write((char *)&br, 4);
    f.write((char *)&align, 2);
    f.write((char *)&bits, 2);
    f.write("data", 4);
    f.write((char *)&data, 4);
    for (int s = 0; s < a.n_samp; s++)
        for (int c = 0; c < a.n_ch; c++)
        {
            float v = std::clamp(a.samples[(size_t)c * a.n_samp + s], -1.f, 1.f);
            int16_t q = (int16_t)(v * 32767.f);
            f.write((char *)&q, 2);
        }
    return !!f;
}
struct PS
{
    CStableAudio3Plugin *p;
    const std::function<void(const std::wstring &, int)> *f;
};
static void prog(void *u, const char *s, int a, int b, float x)
{
    auto *p = (PS *)u;
    (*p->f)(W(s ? s : "working") + L" " + std::to_wstring(a) + L"/" + std::to_wstring(b),
            std::clamp((int)(x * 100), 0, 100));
}
static int cancelled(void *u)
{
    return ((CStableAudio3Plugin *)u)->cancel ? 1 : 0;
}
bool GenerateAudio(CStableAudio3Plugin &p, const GenerationOptions &o, const std::filesystem::path &out,
                   const std::function<void(const std::wstring &, int)> &fn, std::wstring &e)
{
    Api a;
    if (!a.load(p.workDirectory / (o.device == L"gpu" ? L"sa3-cpp-gpu" : L"sa3-cpp-cpu") / L"sa3.dll", e))
        return false;
    auto md = U((p.workDirectory / L"models").wstring()), v = U(o.model), en = U(o.encoding), dev = U(o.device);
    sa3_config_ex c{};
    c.config.models_dir = md.c_str();
    c.config.adapters_dir = md.c_str();
    c.config.variant = v.c_str();
    c.config.encoding = en.c_str();
    c.cpu_threads = o.threads;
    c.device = dev.c_str();
    char err[2048]{};
    auto *ctx = a.i(&c, err, sizeof(err));
    if (!ctx)
    {
        e = W(err);
        return false;
    }
    auto prompt = U(o.prompt), neg = U(o.negativePrompt), shift = U(o.distShift);
    PS ps{&p, &fn};
    sa3_request_ex r{};
    r.request.prompt = prompt.c_str();
    r.request.negative_prompt = neg.empty() ? nullptr : neg.c_str();
    r.request.frames = o.frames;
    r.request.steps = o.steps;
    r.request.seed = o.seed;
    r.request.cfg_scale = o.cfg;
    r.request.duration_padding_sec = o.padding;
    r.request.keep_models = o.keepModels;
    r.request.dist_shift = shift.c_str();
    r.request.on_progress = prog;
    r.request.user = &ps;
    r.should_cancel = cancelled;
    r.cancel_user = &p;
    sa3_audio audio{};
    int rc = a.g(ctx, &r, &audio, err, sizeof(err));
    bool ok = !rc && wav(out, audio, e);
    if (rc)
        e = p.cancel ? L"Generation cancelled" : W(err);
    if (audio.samples)
        a.fa(&audio);
    a.fc(ctx);
    return ok;
}
struct MF
{
    std::wstring repo, name;
};
static std::vector<MF> files(const std::wstring &m, const std::wstring &e)
{
    auto enc = e == L"f32" ? L"F32" : L"F16";
    bool med = m == L"medium";
    std::wstring size = med ? L"1.5B" : L"0.5B", same = med ? L"same-l" : L"same-s", base = L"stable-audio-3-" + m,
                 repo = L"thepatch/" + base + L"-GGUF";
    return {{repo, base + L"-dit-" + size + L"-v1.0-" + enc + L".gguf"},
            {repo, base + L"-" + same + L"-v1.0-" + enc + L".gguf"},
            {repo, base + L"-conditioner-v1.0-F32.gguf"},
            {L"thepatch/t5gemma-b-b-ul2-GGUF", L"t5gemma-b-b-ul2-encoder-0.3B-v1.0-F32.gguf"},
            {L"thepatch/t5gemma-b-b-ul2-GGUF", L"t5gemma-b-b-ul2-v1.0-vocab.gguf"}};
}
bool IsModelSetPresent(const std::filesystem::path &p, const std::wstring &m, const std::wstring &e)
{
    for (auto &f : files(m, e))
        if (!std::filesystem::exists(p / f.name))
            return false;
    return true;
}
namespace
{
struct InternetHandle
{
    HINTERNET value = nullptr;
    ~InternetHandle()
    {
        if (value)
            WinHttpCloseHandle(value);
    }
};

std::wstring FormatBytes(unsigned long long bytes)
{
    std::wostringstream stream;
    stream << std::fixed << std::setprecision(1);
    if (bytes >= 1024ull * 1024ull * 1024ull)
        stream << static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0) << L" GB";
    else
        stream << static_cast<double>(bytes) / (1024.0 * 1024.0) << L" MB";
    return stream.str();
}

bool DownloadFile(CStableAudio3Plugin &plugin, const std::wstring &url, const std::filesystem::path &destination,
                  int fileIndex, int fileCount, const std::function<void(const std::wstring &, int)> &progress,
                  std::wstring &error)
{
    URL_COMPONENTS parts{sizeof(parts)};
    wchar_t host[512]{};
    wchar_t path[4096]{};
    parts.lpszHostName = host;
    parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = static_cast<DWORD>(std::size(path));
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts))
    {
        error = L"Invalid model URL";
        return false;
    }

    InternetHandle session{WinHttpOpen(L"AVS StableAudio3/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                       WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value)
    {
        error = L"Cannot initialize model download";
        return false;
    }
    InternetHandle connection{WinHttpConnect(session.value, host, parts.nPort, 0)};
    if (!connection.value)
    {
        error = L"Cannot connect to the model server";
        return false;
    }
    InternetHandle request{WinHttpOpenRequest(connection.value, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                              WINHTTP_DEFAULT_ACCEPT_TYPES,
                                              parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)};
    if (!request.value ||
        !WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.value, nullptr))
    {
        error = L"Unable to start model download (WinHTTP error " + std::to_wstring(GetLastError()) + L")";
        return false;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
    if (status < 200 || status >= 300)
    {
        error = L"Model server returned HTTP " + std::to_wstring(status);
        return false;
    }

    unsigned long long contentLength = 0;
    wchar_t lengthText[64]{};
    DWORD lengthSize = sizeof(lengthText);
    if (WinHttpQueryHeaders(request.value, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lengthText,
                            &lengthSize, WINHTTP_NO_HEADER_INDEX))
    {
        try
        {
            contentLength = std::stoull(lengthText);
        }
        catch (...)
        {
            contentLength = 0;
        }
    }

    const auto partial = destination.wstring() + L".part";
    std::ofstream output(partial, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        error = L"Cannot create model file: " + destination.wstring();
        return false;
    }

    std::vector<char> buffer(1024 * 1024);
    unsigned long long received = 0;
    for (;;)
    {
        if (plugin.cancel)
        {
            output.close();
            std::filesystem::remove(partial);
            error = L"Download cancelled";
            return false;
        }
        DWORD bytesRead = 0;
        if (!WinHttpReadData(request.value, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead))
        {
            output.close();
            std::filesystem::remove(partial);
            error = L"Model download failed (WinHTTP error " + std::to_wstring(GetLastError()) + L")";
            return false;
        }
        if (bytesRead == 0)
            break;
        output.write(buffer.data(), bytesRead);
        if (!output)
        {
            output.close();
            std::filesystem::remove(partial);
            error = L"Cannot write model file. Check free disk space.";
            return false;
        }
        received += bytesRead;
        int filePercent = contentLength ? static_cast<int>((received * 100) / contentLength) : 0;
        filePercent = std::clamp(filePercent, 0, 99);
        int overall = ((fileIndex * 100) + filePercent) / fileCount;
        std::wstring message = L"Downloading " + destination.filename().wstring() + L": " + FormatBytes(received);
        if (contentLength)
            message += L" / " + FormatBytes(contentLength) + L" (" + std::to_wstring(filePercent) + L"%)";
        progress(message, overall);
    }
    output.close();

    if (contentLength && received != contentLength)
    {
        std::filesystem::remove(partial);
        error = L"Incomplete model download: " + destination.filename().wstring();
        return false;
    }
    std::error_code filesystemError;
    std::filesystem::rename(partial, destination, filesystemError);
    if (filesystemError)
    {
        std::filesystem::remove(destination, filesystemError);
        filesystemError.clear();
        std::filesystem::rename(partial, destination, filesystemError);
    }
    if (filesystemError)
    {
        error = L"Cannot finalize model file: " + destination.filename().wstring();
        return false;
    }
    return true;
}
} // namespace

bool DownloadModelSet(CStableAudio3Plugin &plugin, const std::wstring &model, const std::wstring &encoding,
                      const std::function<void(const std::wstring &, int)> &progress, std::wstring &error)
{
    const auto directory = plugin.workDirectory / L"models";
    std::error_code filesystemError;
    std::filesystem::create_directories(directory, filesystemError);
    const auto modelFiles = files(model, encoding);

    for (size_t index = 0; index < modelFiles.size(); ++index)
    {
        if (plugin.cancel)
        {
            error = L"Download cancelled";
            return false;
        }
        const auto &file = modelFiles[index];
        const auto destination = directory / file.name;
        if (std::filesystem::exists(destination))
        {
            progress(L"Already present: " + file.name, static_cast<int>((index + 1) * 100 / modelFiles.size()));
            continue;
        }
        const std::wstring url = L"https://huggingface.co/" + file.repo + L"/resolve/main/" + file.name;
        if (!DownloadFile(plugin, url, destination, static_cast<int>(index), static_cast<int>(modelFiles.size()),
                          progress, error))
            return false;
        progress(L"Downloaded: " + file.name, static_cast<int>((index + 1) * 100 / modelFiles.size()));
    }
    return true;
}
