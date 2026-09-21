import MediaPlayer

/// 媒体键（macOS 的 MPRIS 等价物）：控制中心 / 键盘媒体键 / AirPods 手势都走这里。
///
/// 设计：与 Linux 端 Mpris 一致 —— 只做"协议翻译"，动作交回主控制器
/// （保证媒体键与界面按钮/快捷键走同一条逻辑）。
final class MediaKeys {
    private static var installed = false
    private let player: MpvView
    weak var delegate: AppDelegate?

    // 供 --test-mediakeys 直接调用（验证的就是注册给系统的同一批 handler）
    var handlerPlay: (() -> Void) = {}
    var handlerPause: (() -> Void) = {}
    var handlerToggle: (() -> Void) = {}
    var handlerNext: (() -> Void) = {}
    var handlerPrev: (() -> Void) = {}
    var handlerSeek: ((Double) -> Void) = { _ in }

    init(player: MpvView) {
        self.player = player
        handlerPlay = { [weak self] in self?.player.togglePauseIfPaused(false) }
        handlerPause = { [weak self] in self?.player.togglePauseIfPaused(true) }
        handlerToggle = { [weak self] in self?.player.togglePause() }
        handlerNext = { [weak self] in self?.delegate?.onNext() }
        handlerPrev = { [weak self] in self?.delegate?.onPrev() }
        handlerSeek = { [weak self] pos in self?.player.seek(to: pos) }
    }

    func setup() {
        let c = MPRemoteCommandCenter.shared()
        c.playCommand.isEnabled = true
        c.pauseCommand.isEnabled = true
        c.togglePlayPauseCommand.isEnabled = true
        c.nextTrackCommand.isEnabled = true
        c.previousTrackCommand.isEnabled = true
        c.changePlaybackPositionCommand.isEnabled = true
        c.playCommand.addTarget { [weak self] _ in self?.fire(self?.handlerPlay) ?? .success }
        c.pauseCommand.addTarget { [weak self] _ in self?.fire(self?.handlerPause) ?? .success }
        c.togglePlayPauseCommand.addTarget { [weak self] _ in self?.fire(self?.handlerToggle) ?? .success }
        c.nextTrackCommand.addTarget { [weak self] _ in self?.fire(self?.handlerNext) ?? .success }
        c.previousTrackCommand.addTarget { [weak self] _ in self?.fire(self?.handlerPrev) ?? .success }
        c.changePlaybackPositionCommand.addTarget { [weak self] ev in
            guard let e = ev as? MPChangePlaybackPositionCommandEvent else { return .commandFailed }
            self?.handlerSeek(e.positionTime)
            return .success
        }
        Config.log("媒体键已就绪（控制中心 / 键盘媒体键 / changePlaybackPosition）")
    }

    private func fire(_ f: (() -> Void)?) -> MPRemoteCommandHandlerStatus {
        f?()
        return .success
    }

    /// 写入"正在播放"信息（控制中心、锁屏、AirPods 弹窗都读它）
    func updateNowPlaying(title: String, artist: String, duration: Double, paused: Bool, position: Double) {
        if CommandLine.arguments.contains("--diag") {
            Config.log("[diag] nowplaying called: titleLen=\(title.count) duration=\(String(format: "%.1f", duration))")
        }
        guard !title.isEmpty, duration > 0 else { return }
        let info: [String: Any] = [
            MPMediaItemPropertyTitle: title,
            MPMediaItemPropertyArtist: artist.isEmpty ? "聚合视频" : artist,
            MPMediaItemPropertyPlaybackDuration: duration,
            MPNowPlayingInfoPropertyElapsedPlaybackTime: position,
            MPNowPlayingInfoPropertyPlaybackRate: paused ? 0.0 : 1.0,
        ]
        MPNowPlayingInfoCenter.default().nowPlayingInfo = info
        if CommandLine.arguments.contains("--diag") {
            let back = MPNowPlayingInfoCenter.default().nowPlayingInfo
            Config.log("[diag] now-playing 写入后回读: \(back == nil ? "nil" : "\(back!.count) 个字段")")
        }
    }

    /// 自动化判据：把当前 now-playing 信息读回来
    func nowPlayingSummary() -> String {
        guard let info = MPNowPlayingInfoCenter.default().nowPlayingInfo else { return "(空)" }
        let t = info[MPMediaItemPropertyTitle] as? String ?? "-"
        let d = info[MPMediaItemPropertyPlaybackDuration] as? Double ?? 0
        let r = info[MPNowPlayingInfoPropertyPlaybackRate] as? Double ?? 0
        return String(format: "title=%@ duration=%.1f rate=%.1f", t, d, r)
    }
}
