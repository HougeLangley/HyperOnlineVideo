#include "CliHelp.h"
#include "HovVersion.h"

#include <cstdio>
#include <cstring>

static bool isOpt(const char *a, const char *name) {
    return a && std::strcmp(a, name) == 0;
}

bool hovHandleHelpOrVersion(int argc, char **argv) {
    bool help = false;
    bool ver = false;
    for (int i = 1; i < argc; ++i) {
        if (isOpt(argv[i], "--help") || isOpt(argv[i], "-h")) help = true;
        else if (isOpt(argv[i], "--version") || isOpt(argv[i], "-V") || isOpt(argv[i], "-v")) ver = true;
    }
    if (!help && !ver) return false;

    if (ver && !help) {
        std::printf("hov-qt %s\n", HOV_APP_VERSION);
        return true;
    }

    std::printf(
        "hov-qt %s — Hyper Online Video (Qt6 / libmpv)\n"
        "\n"
        "Usage: hov-qt [options]\n"
        "\n"
        "  -h, --help              Show this help and exit\n"
        "  -V, -v, --version       Print version and exit\n"
        "\n"
        "Playback / UI:\n"
        "  --open <file|url...>    Open local files or page URLs\n"
        "  --demo <keyword>        Search on startup\n"
        "  --source <0|1|2|3>      0=YouTube 1=NetEase 2=QQ Music 3=local library\n"
        "  --music <keyword>       NetEase: search and play first hit\n"
        "  --qqmusic <keyword>     QQ Music: search and play first hit\n"
        "  --video-quality <auto|audio|360|480|720|1080>\n"
        "  --quality <standard|exhigh|lossless>\n"
        "  --speed <0.25-4>        Playback speed\n"
        "  --autoplay              Start playback without extra click\n"
        "  --no-resume             Do not restore last position\n"
        "  --local                 Open the downloads / local library view\n"
        "  --login [site]          youtube|bilibili|netease|qqmusic\n"
        "  --settings              Open the settings dialog\n"
        "  --filters               Open search filters\n"
        "  --sub-track <n>         Subtitle track index (-1=off)\n"
        "  --sub-delay <sec>       Subtitle delay\n"
        "  --subs-url <url>        Load an external subtitle URL\n"
        "\n"
        "Downloads / maintenance (no display required):\n"
        "  --download <url> [name]\n"
        "  --download-music <keyword>\n"
        "  --download-count <n>\n"
        "  --cleanup-downloads [--max-total-mb <n>]\n"
        "  --import-cookies [browser]\n"
        "  --progress-show | --progress-clear\n"
        "  --show-settings         Dump ~/.config/hov/settings.json\n"
        "  --set key=value         Change a setting (repeatable)\n"
        "\n"
        "Automation:\n"
        "  --queue-selftest        Pure logic self-test, then idle until --exit-after\n"
        "  --selftest <file>       Isolated mpv render path\n"
        "  --exit-after <sec>      Quit after N seconds\n"
        "\n"
        "Environment: HOV_QPA, QT_QPA_PLATFORM=offscreen (headless), HOV_MPV_VERBOSE=1\n",
        HOV_APP_VERSION);
    return true;
}
