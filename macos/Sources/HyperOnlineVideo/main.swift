import AppKit

// 无缓冲输出，便于自动化读取日志
setvbuf(stdout, nil, _IONBF, 0)
setvbuf(stderr, nil, _IONBF, 0)

// 先跑纯命令行模式（自检/设置/取流验证）：这些不需要窗口，做完直接退出
let decision = Cli.run(CommandLine.arguments)
switch decision {
case .exit(let code):
    exit(code)
case .play(let video, let audio, let label):
    let app = NSApplication.shared
    let delegate = AppDelegate()
    delegate.initialTarget = video
    delegate.initialAudio = audio
    delegate.initialLabel = label
    app.delegate = delegate
    app.setActivationPolicy(.regular)
    app.run()
default:
    let app = NSApplication.shared
    let delegate = AppDelegate()
    app.delegate = delegate
    app.setActivationPolicy(.regular)
    app.run()
}
