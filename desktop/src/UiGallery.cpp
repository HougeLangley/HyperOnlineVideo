#include "Icons.h"
#include "Ui.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>

namespace {

QHBoxLayout *row(QVBoxLayout *root, const QString &title, QWidget *parent)
{
    root->addWidget(Ui::label(title, Ui::TextRole::CaptionVariant, parent));
    auto *h = new QHBoxLayout();
    h->setSpacing(M::space2);
    root->addLayout(h);
    return h;
}

}  // namespace

QWidget *Ui::gallery()
{
    auto *page = new QWidget();
    page->setObjectName("hovGallery");
    page->setWindowTitle(QStringLiteral("组件陈列"));
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(M::space6, M::space6, M::space6, M::space6);
    root->setSpacing(M::space3);

    auto *h = row(root, QStringLiteral("按钮：filled / tonal / text / 禁用"), page);
    h->addWidget(filledButton(QStringLiteral("搜索"), page));
    h->addWidget(filledButton(IconId::Check, QStringLiteral("应用过滤器"), page));
    h->addWidget(tonalButton(IconId::Refresh, QStringLiteral("重试失败"), page));
    h->addWidget(textButton(QStringLiteral("取消"), page));
    auto *dis = tonalButton(IconId::Delete, QStringLiteral("清空"), page);
    dis->setEnabled(false);
    h->addWidget(dis);
    h->addStretch(1);

    h = row(root, QStringLiteral("图标按钮：标准 / 小 / 勾选 / 播放"), page);
    h->addWidget(iconButton(IconId::Download, QStringLiteral("下载"), page));
    h->addWidget(iconButton(IconId::MoreHoriz, QStringLiteral("更多"), page));
    auto *checked = iconButton(IconId::Autoplay, QStringLiteral("连播"), page, IconSize::Standard, true);
    checked->setChecked(true);
    h->addWidget(checked);
    h->addWidget(iconButton(IconId::Close, QStringLiteral("取消下载"), page, IconSize::Small));
    h->addWidget(playButton(page));
    h->addStretch(1);

    h = row(root, QStringLiteral("芯片"), page);
    auto *c1 = chip(IconId::SmartDisplay, QStringLiteral("YouTube"), page);
    c1->setChecked(true);
    h->addWidget(c1);
    h->addWidget(chip(IconId::LiveTv, QStringLiteral("B站"), page));
    h->addWidget(chip(IconId::MusicNote, QStringLiteral("网易云音乐"), page));
    h->addStretch(1);

    h = row(root, QStringLiteral("输入"), page);
    h->addWidget(searchField(QStringLiteral("搜索视频或歌曲"), page), 1);
    auto *box = new QComboBox(page);
    box->addItems({QStringLiteral("自动"), QStringLiteral("1080p"), QStringLiteral("720p")});
    asSelect(box);
    h->addWidget(box);

    h = row(root, QStringLiteral("复选 / 单选 / 滑块"), page);
    auto *cb = new QCheckBox(QStringLiteral("已勾选"), page);
    cb->setChecked(true);
    h->addWidget(cb);
    h->addWidget(new QCheckBox(QStringLiteral("未勾选"), page));
    auto *rb = new QRadioButton(QStringLiteral("相关度"), page);
    rb->setChecked(true);
    h->addWidget(rb);
    h->addWidget(new QRadioButton(QStringLiteral("最新"), page));
    auto *slider = new QSlider(Qt::Horizontal, page);
    slider->setValue(60);
    h->addWidget(slider, 1);

    h = row(root, QStringLiteral("进度条：进行中 / 完成 / 失败"), page);
    const ProgressState states[] = {ProgressState::Running, ProgressState::Done, ProgressState::Failed};
    for (ProgressState s : states) {
        auto *bar = progressBar(page);
        bar->setValue(s == ProgressState::Running ? 42 : 100);
        setProgressState(bar, s);
        h->addWidget(bar, 1);
    }

    auto *p = panel(page);
    auto *pl = new QVBoxLayout(p);
    pl->setContentsMargins(M::space4, M::space4, M::space4, M::space4);
    pl->setSpacing(M::space1);
    pl->addWidget(label(QStringLiteral("面板标题 Title"), TextRole::Title, p));
    pl->addWidget(label(QStringLiteral("正文 Body：面板放在 Surface 上，边框用 OutlineVariant。"), TextRole::Body, p));
    pl->addWidget(label(QStringLiteral("说明 CaptionVariant · 0:00 / 3:16"), TextRole::CaptionVariant, p));
    root->addWidget(p);

    root->addWidget(label(QStringLiteral("图标登记表（Outlined / Filled）"), TextRole::CaptionVariant, page));
    auto *grid = new QGridLayout();
    grid->setSpacing(M::space2);
    int i = 0;
    for (IconId id : kAllIcons) {
        auto *b = iconButton(id, QString::fromLatin1(kAllIconNames[i]), page, IconSize::Standard, true);
        b->setChecked(i % 2 == 1);
        grid->addWidget(b, i / 16, i % 16);
        ++i;
    }
    root->addLayout(grid);
    root->addStretch(1);
    return page;
}
