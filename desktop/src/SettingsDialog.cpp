// ── SettingsDialog.cpp：由 main.cpp 搬出的设置对话框（S2 ✓ 零行为改动 ✓）──
// 搬迁记录：main.cpp 第 750-848 行（99 行）✓ 2026-09-22
#include "SettingsDialog.h"
#include "Settings.h"
#include "MpvWidget.h"
#include <QListWidget>
#include <QGuiApplication>
#include <QObject>
#include "DownloadManager.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QStringList>
#include <QString>
#include <cmath>
#include <iostream>

void SettingsDialog::open(QWidget *parent, const Ctx &ctx) {
        QDialog dlg(parent);
        dlg.setWindowTitle("设置");
        dlg.setMinimumWidth(520);
        auto *form = new QFormLayout(&dlg);

        auto *quality = new QComboBox(&dlg);
        quality->addItems({ "standard（标准 128k）", "exhigh（较高 320k）", "lossless（无损，需会员）" });
        quality->setCurrentIndex(qBound(0, QStringList{ "standard", "exhigh", "lossless" }
                                               .indexOf(ctx.settings->str("music.qualityCeiling", "exhigh")), 1));

        auto *fontScale = new QDoubleSpinBox(&dlg);
        fontScale->setRange(0.5, 2.0);
        fontScale->setSingleStep(0.25);
        fontScale->setValue(ctx.settings->number("subtitle.fontScale", 1.0));

        auto *delay = new QDoubleSpinBox(&dlg);
        delay->setRange(-5.0, 5.0);
        delay->setSingleStep(0.5);
        delay->setSuffix(" 秒");
        delay->setValue(ctx.settings->number("subtitle.defaultDelay", 0.0));

        auto *karaoke = new QCheckBox("歌词用卡拉OK面板（居中 + 逐字高亮）", &dlg);
        karaoke->setChecked(ctx.settings->boolean("subtitle.karaoke", true));

        auto *probe = new QCheckBox("启动时做网络探活", &dlg);
        probe->setChecked(ctx.settings->boolean("ui.showNetworkProbe", true));

        // 观感三项（键早已存在 ✓ 之前设置界面点不到 ✗ —— 规则 #5 一致性清单本轮补齐 ✓）
        auto *glassCheck = new QCheckBox("玻璃质感（封面磨砂底 + 主题色渐变）", &dlg);
        glassCheck->setChecked(ctx.settings->boolean("ui.glass", true));
        auto *glassWinCheck = new QCheckBox("窗口级透明（桌面透出；Wayland 下可能隐藏视频画面，谨慎开）", &dlg);
        glassWinCheck->setChecked(ctx.settings->boolean("ui.glassWindow", QGuiApplication::platformName().contains("xcb")));
        auto *hoverCheck = new QCheckBox("全屏时鼠标贴左边缘浮出结果侧栏", &dlg);
        hoverCheck->setChecked(ctx.settings->boolean("ui.hoverReveal", true));

        auto *autoNextCheck = new QCheckBox("播完自动播下一条（按列表顺序连播）", &dlg);   // UI-4 ✓ 与 macOS/Android 同键 ✓
        autoNextCheck->setChecked(ctx.settings->boolean("playback.autoNext", true));

        auto *direct = new QCheckBox("国内接口强制直连（音乐/B站，忽略代理）", &dlg);
        direct->setChecked(ctx.settings->boolean("network.forceDirectDomestic", false));

        auto *dirEdit = new QLineEdit(ctx.settings->str("download.dir", DownloadManager::defaultDir()), &dlg);
        auto *dirRow = new QWidget(&dlg);
        auto *dirLay = new QHBoxLayout(dirRow);
        dirLay->setContentsMargins(0, 0, 0, 0);
        dirLay->addWidget(dirEdit, 1);
        auto *browse = new QPushButton("浏览…", dirRow);
        dirLay->addWidget(browse);
        QObject::connect(browse, &QPushButton::clicked, parent, [parent, dirEdit, &ctx] {
            const QString d = QFileDialog::getExistingDirectory(parent, "选择下载目录", dirEdit->text());
            if (!d.isEmpty()) dirEdit->setText(d);
        });

        auto *themeBox = new QComboBox(&dlg);
        themeBox->addItem("跟随系统（浅色 / 深色）", "auto");
        themeBox->addItem("浅色", "light");
        themeBox->addItem("深色", "dark");
        {
            const QString cur = ctx.settings->str("ui.theme", "auto").trimmed().toLower();
            const int idx = themeBox->findData(cur);
            themeBox->setCurrentIndex(idx >= 0 ? idx : 0);
        }
        form->addRow("应用主题", themeBox);
        form->addRow("音质上限", quality);
        form->addRow("字幕/歌词字号", fontScale);
        form->addRow("默认字幕延迟", delay);
        form->addRow(QString(), karaoke);
        form->addRow(QString(), glassCheck);
        form->addRow(QString(), glassWinCheck);
        form->addRow(QString(), hoverCheck);
        form->addRow(QString(), autoNextCheck);
        form->addRow(QString(), probe);
        form->addRow(QString(), direct);
        form->addRow("下载目录", dirRow);

        auto *btns = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close, &dlg);
        form->addRow(btns);
        QObject::connect(btns, &QDialogButtonBox::accepted, &dlg, [parent, quality, fontScale, delay, karaoke, probe, direct, dirEdit, themeBox, glassCheck, glassWinCheck, hoverCheck, &dlg, &ctx, autoNextCheck] {
            static const QStringList qs{ "standard", "exhigh", "lossless" };
            ctx.applySetting("music.qualityCeiling", qs.at(quality->currentIndex()));
            ctx.applySetting("ui.theme", themeBox->currentData().toString());
            ctx.applyThemeSettingNow();                                  // 立即生效（无需重启 ✓）
            ctx.applySetting("subtitle.fontScale", QString::number(fontScale->value()));
            ctx.applySetting("subtitle.defaultDelay", QString::number(delay->value()));
            ctx.applySetting("subtitle.karaoke", karaoke->isChecked() ? "true" : "false");
            ctx.applySetting("ui.glass", glassCheck->isChecked() ? "true" : "false");
            if (ctx.player) ctx.player->setGlassEnabled(glassCheck->isChecked());          // 立即生效 ✓
            const bool gwWas = ctx.settings->boolean("ui.glassWindow", false);
            ctx.applySetting("ui.glassWindow", glassWinCheck->isChecked() ? "true" : "false");
            if (gwWas != glassWinCheck->isChecked())
                ctx.setStatusLine(QString("窗口级透明已设为 %1 —— 重新打开应用后生效")
                                  .arg(glassWinCheck->isChecked() ? "开" : "关"));   // 切透明要重建窗口 → 提示重启 ✓
            ctx.applySetting("ui.hoverReveal", hoverCheck->isChecked() ? "true" : "false");  // 轮询里实时读 ✓ 立即生效
            ctx.applySetting("playback.autoNext", autoNextCheck->isChecked() ? "true" : "false");
            ctx.applySetting("ui.showNetworkProbe", probe->isChecked() ? "true" : "false");
            ctx.applySetting("network.forceDirectDomestic", direct->isChecked() ? "true" : "false");
            ctx.applySetting("download.dir", dirEdit->text().trimmed());
            ctx.results->addItem("设置已保存");
            dlg.accept();
        });
        QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        dlg.exec();
}
