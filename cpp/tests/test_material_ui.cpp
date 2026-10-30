#include "core/material_style.h"
#include "core/ui_theme.h"
#include "core/theme.h"
#include "settings/tool_settings.h"
#include "clipboard/core/manager.h"
#include "clipboard/ui/windows/clipboard_window.h"
#include "clipboard/ui/theme/themes.h"
#include "translation/translation_dialog.h"
#include "translation/translation_popup.h"
#include "ui/dialogs.h"
#include "ui/fluent_lite/buttons.h"
#include "ui/fluent_lite/cards.h"
#include "ui/fluent_lite/frameless.h"
#include "ui/fluent_lite/inputs.h"
#include "ui/fluent_lite/switch.h"
#include "ui/fluent_lite/theme.h"
#include "ui/color_picker_dialog.h"
#include "ui/save_image_dialog.h"
#include "ui/settings_ui/dialog.h"
#include "ui/toolbar.h"
#include "ui/arrow_settings_panel.h"
#include "core/ui_scale.h"
#include "ui/fluent_lite/titlebar.h"
#include "ui/fluent_lite/navigation.h"
#include <QScrollArea>
#include "ui/welcome/wizard.h"
#include <QApplication>
#include <QDir>
#include <QEnterEvent>
#include <QFontDatabase>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QScopeGuard>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QVBoxLayout>

using namespace jietuba;

class MaterialUiTests : public QObject {
    Q_OBJECT
    QTemporaryDir directory_;
    QSettings *settings_ = nullptr;
    ToolSettingsManager *config_ = nullptr;
    QString mode_;
    void snapshot(QWidget &widget, const QString &name, bool hide_after=true) {
        const QString output=qEnvironmentVariable("JIETUMA_MATERIAL_VISUAL_DIR");
        widget.show();
        QApplication::processEvents();
        QTest::qWait(40);
        const auto pixmap=widget.grab();
        QVERIFY(!pixmap.isNull());
        if(!output.isEmpty()) {
            QDir().mkpath(output);
            QImage image(pixmap.size()+QSize(24,24),QImage::Format_ARGB32_Premultiplied);
            image.fill(mode_=="dark"?QColor("#171B20"):QColor("#DDE1E7"));
            {QPainter painter(&image);painter.drawPixmap(12,12,pixmap);}
            QVERIFY(image.save(QDir(output).filePath(mode_+"-"+name+".png")));
        }
        if(hide_after) widget.hide();
    }
private slots:
    void initTestCase() {
        QVERIFY(directory_.isValid());
        settings_=new QSettings(directory_.filePath("settings.ini"),QSettings::IniFormat,this);
        config_=get_tool_settings_manager(settings_);
        config_->set_language("en");
        config_->set_hotkey(""); config_->set_clipboard_hotkey("");
        config_->set_clipboard_enabled(false);
        get_theme().init(config_);
        get_ui_theme()->init(config_,qApp);
        const int font=QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");
        if(font>=0 && !QFontDatabase::applicationFontFamilies(font).isEmpty()) QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(font).first()));
        QVERIFY(clipboard::ClipboardManager::instance().reset_storage(directory_.filePath("clipboard.db")));
    }
    void normalizationKeepsSemanticFillsAndRemovesDecorativeStrokes_data() {
        QTest::addColumn<QString>("mode");
        QTest::newRow("light")<<QString("light"); QTest::newRow("dark")<<QString("dark");
    }
    void normalizationKeepsSemanticFillsAndRemovesDecorativeStrokes() {
        QFETCH(QString,mode); get_ui_theme()->set_mode(mode);
        const QString input="QLineEdit {background:#FFFFFF; color:#333333;border:1px solid #555555;border-radius:10px;} QLineEdit:focus{border-color:#007aff;} QPushButton:checked{background:#007aff;}";
        const auto output=material_stylesheet(nullptr,input);
        QVERIFY(output.contains("border:1px solid transparent"));
        QVERIFY(output.contains("border-color: transparent"));
        QVERIFY(output.contains("background:#007aff"));
        QVERIFY(output.contains("background:"+get_ui_theme()->tokens().window));
        QCOMPARE(material_stylesheet(nullptr,output),output);
        ColorSwatchButton swatch;
        swatch.setFill("#FFFFFF");
        QVERIFY(material_stylesheet(&swatch,"QPushButton {background:#FFFFFF;}").contains("background:#FFFFFF"));
    }
    void raisedAndRecessedUseOppositeDepthInBothThemes_data() {
        normalizationKeepsSemanticFillsAndRemovesDecorativeStrokes_data();
    }
    void raisedAndRecessedUseOppositeDepthInBothThemes() {
        QFETCH(QString,mode); get_ui_theme()->set_mode(mode);
        const QColor base(get_ui_theme()->tokens().window);
        const auto render=[&](MaterialDepth depth) {
            QImage image(120,80,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
            QPainter painter(&image);paint_material_surface(painter,QRectF(1,1,118,78),12,base,depth);painter.end();return image;
        };
        const auto raised=render(MaterialDepth::Raised), recessed=render(MaterialDepth::Recessed);
        QCOMPARE(raised.pixelColor(60,40),base);QCOMPARE(recessed.pixelColor(60,40),base);
        QVERIFY(raised.pixelColor(30,3).lightness()>recessed.pixelColor(30,3).lightness());
        QVERIFY(raised.pixelColor(90,76).lightness()<recessed.pixelColor(90,76).lightness());
        QCOMPARE(recessed.pixelColor(30,3),recessed.pixelColor(90,3));
        QCOMPARE(recessed.pixelColor(3,20),recessed.pixelColor(3,60));
        QCOMPARE(raised.pixelColor(0,0).alpha(),0);
    }
    void sharedControlsAndApplicationWindows_data() {
        normalizationKeepsSemanticFillsAndRemovesDecorativeStrokes_data();
    }
    void embeddedPanelsPreserveTheCaptureBackground_data() { normalizationKeepsSemanticFillsAndRemovesDecorativeStrokes_data(); }
    void embeddedPanelsPreserveTheCaptureBackground() {
        QFETCH(QString,mode);mode_=mode;get_ui_theme()->set_mode(mode);
        QWidget canvas;canvas.resize(1100,260);canvas.setAutoFillBackground(true);
        const QColor backdrop("#78A3C2");auto palette=canvas.palette();palette.setColor(QPalette::Window,backdrop);canvas.setPalette(palette);
        Toolbar toolbar(&canvas);toolbar.move(24,24);toolbar.show();
        for(const QString tool : {QString("arrow"),QString("pen"),QString("rect"),QString("number"),QString("text"),QString("mosaic"),QString("watermark")}) {
          auto *panel=toolbar.ensure_panel_for_tool(tool);QVERIFY(panel);panel->move(24,94);panel->show();
          canvas.resize(std::max(1100,panel->width()+48),260);
          snapshot(canvas,"embedded-"+tool,false);QApplication::processEvents();
          const auto image=canvas.grab().toImage();
          for(auto *surface : {static_cast<QWidget *>(&toolbar),panel}) {
            QVERIFY(!surface->isWindow());
            for(const QPoint point : {QPoint(0,0),QPoint(surface->width()-1,0),QPoint(0,surface->height()-1),QPoint(surface->width()-1,surface->height()-1)}) {
                const auto color=image.pixelColor(surface->pos()+point);
                QCOMPARE(color.alpha(),255);
                QVERIFY2(color.lightness()>80,qPrintable(color.name()));
                QVERIFY(qAbs(color.red()-backdrop.red())<12 && qAbs(color.blue()-backdrop.blue())<12);
            }
          }
          panel->hide();
        }
    }
    void captionControlsStayInsideTheMaterialSurface() {
        SettingsDialog dialog(config_);dialog.show();QApplication::processEvents();
        auto *bar=dialog.titleBar();QVERIFY(bar);
        QVERIFY(bar->geometry().left()>=widget_scaled(&dialog,5));
        QVERIFY(bar->geometry().right()<dialog.width()-widget_scaled(&dialog,5));
        for(auto *button : bar->findChildren<QAbstractButton *>()) {
            QVERIFY(!button->findChild<QWidget *>("_materialEdge",Qt::FindDirectChildrenOnly));
        }
    }
    void toolbarHoverStaysInsideItsRoundedBody_data() {
        QTest::addColumn<QString>("mode");QTest::addColumn<int>("scale");
        for(const QString mode : {QString("light"),QString("dark")})
            for(int scale : {100,150}) QTest::newRow(qPrintable(mode+QString::number(scale)))<<mode<<scale;
    }
    void toolbarHoverStaysInsideItsRoundedBody() {
        QFETCH(QString,mode);QFETCH(int,scale);mode_=mode;get_ui_theme()->set_mode(mode);
        const int old_scale=get_ui_scale().percent();get_ui_scale().set_percent(scale);
        qApp->setProperty("_materialReducedMotion",true);
        const auto restore=qScopeGuard([&]{qApp->setProperty("_materialReducedMotion",false);get_ui_scale().set_percent(old_scale);});
        QWidget canvas;canvas.setAutoFillBackground(true);auto palette=canvas.palette();palette.setColor(QPalette::Window,QColor("#78A3C2"));canvas.setPalette(palette);
        Toolbar toolbar(&canvas);toolbar.move(24,24);canvas.resize(toolbar.width()+48,toolbar.height()+48);canvas.show();toolbar.show();QApplication::processEvents();
        const auto baseline=canvas.grab().toImage();
        const int inset=toolbar.property("_materialSurfaceInset").toInt();
        const int radius=toolbar.property("_materialCornerRadius").toInt();
        QPainterPath body;body.addRoundedRect(QRectF(toolbar.rect()).adjusted(inset,inset,-inset,-inset),radius,radius);
        int count=0;
        for(auto *button : toolbar.findChildren<QPushButton *>(QString(),Qt::FindDirectChildrenOnly)) {
            if(!button->isVisible()) continue;
            QVERIFY(button->y()>=inset);QVERIFY(button->geometry().bottom()<toolbar.height()-inset);
            QEnterEvent enter(QPointF(10,10),QPointF(10,10),QPointF(10,10));QApplication::sendEvent(button,&enter);
            button->setAttribute(Qt::WA_UnderMouse,true);button->update();QApplication::processEvents();
            const auto hovered=canvas.grab().toImage();
            for(int y=-3;y<toolbar.height()+3;++y) for(int x=-3;x<toolbar.width()+3;++x) {
                if(body.contains(QPointF(x+0.5,y+0.5))) continue;
                const QPoint point=toolbar.pos()+QPoint(x,y);
                QCOMPARE(hovered.pixelColor(point),baseline.pixelColor(point));
            }
            if(count++==0) snapshot(canvas,"toolbar-hover-"+QString::number(scale),false);
            button->setAttribute(Qt::WA_UnderMouse,false);QEvent leave(QEvent::Leave);QApplication::sendEvent(button,&leave);QApplication::processEvents();
        }
        QVERIFY(count>=10);
    }
    void navigationLeavesRoomForBothSidesOfTheShadow_data() { normalizationKeepsSemanticFillsAndRemovesDecorativeStrokes_data(); }
    void navigationLeavesRoomForBothSidesOfTheShadow() {
        QFETCH(QString,mode);mode_=mode;get_ui_theme()->set_mode(mode);
        FrostedFramelessDialog host;host.resize(320,390);
        auto *layout=new QVBoxLayout(&host);layout->setContentsMargins(18,42,18,18);
        auto *nav=new NavigationInterface(&host);layout->addWidget(nav);
        nav->addItem("pin",FluentIcon::Pin,"Pin",{});
        auto *item=nav->addItem("annotation",FluentIcon::Edit,"Screen Annotation",{});
        nav->addItem("clipboard",FluentIcon::Document,"Clipboard",{});
        auto *bottom=nav->addItem("about",FluentIcon::Setting,"About",{},NavigationItemPosition::BOTTOM);
        host.show();QApplication::processEvents();
        auto *scroll=nav->findChild<QScrollArea *>();QVERIFY(scroll);
        const QPoint position=item->mapTo(scroll->viewport(),QPoint());
        QVERIFY(position.x()>=dialog_scaled(12));
        QVERIFY(position.x()+item->width()<=scroll->viewport()->width()-dialog_scaled(12));
        QVERIFY(bottom->x()>=dialog_scaled(12));
        QVERIFY(bottom->x()+bottom->width()<=nav->width()-dialog_scaled(12));
        QEnterEvent enter(QPointF(12,12),QPointF(12,12),QPointF(12,12));QApplication::sendEvent(item,&enter);QTest::qWait(340);
        snapshot(host,"navigation-hover");
    }
    void buttonShadowAnimatesWithoutMovingItsLayout() {
        QWidget host;host.resize(300,100);auto *button=new PrimaryPushButton("Save",&host);button->setGeometry(40,30,160,40);
        host.show();QApplication::processEvents();const auto geometry=button->geometry();
        QEvent leave(QEvent::Leave);QApplication::sendEvent(button,&leave);QTest::qWait(340);
        snapshot(host,"button-idle",false);
        QEnterEvent enter(QPointF(10,10),QPointF(50,40),QPointF(50,40));QApplication::sendEvent(button,&enter);QTest::qWait(100);
        const auto progress=button->property("_materialHover").toReal();
        snapshot(host,"button-hover-middle",false);
        QVERIFY(progress>0);QTest::qWait(250);QVERIFY(button->property("_materialHover").toReal()>0.99);
        snapshot(host,"button-hover-end",false);
        QCOMPARE(button->geometry(),geometry);
        QApplication::sendEvent(button,&leave);QTest::qWait(340);QVERIFY(button->property("_materialHover").toReal()<0.01);
        qApp->setProperty("_materialReducedMotion",true);
        const auto restore=qScopeGuard([]{qApp->setProperty("_materialReducedMotion",false);});
        QApplication::sendEvent(button,&enter);QCOMPARE(button->property("_materialHover").toReal(),1.0);
    }
    void onlyPrimaryButtonsGetDepthAnimation() {
        QWidget host;auto *layout=new QVBoxLayout(&host);
        auto *normal=new PushButton("Reset");auto *primary=new PrimaryPushButton("Save");
        auto *nav=new NavigationInterface;auto *item=nav->addItem("capture",FluentIcon::Camera,"Capture",{});
        layout->addWidget(normal);layout->addWidget(primary);layout->addWidget(nav);host.show();QApplication::processEvents();
        item->setChecked(true);
        for(auto *button : {static_cast<QAbstractButton *>(normal),static_cast<QAbstractButton *>(item)}) {
            QEnterEvent enter(QPointF(8,8),QPointF(8,8),QPointF(8,8));QApplication::sendEvent(button,&enter);
            QVERIFY(!button->findChild<QWidget *>("_materialEdge",Qt::FindDirectChildrenOnly));
            QVERIFY(!button->property("_materialHover").isValid());
        }
        QVERIFY(primary->findChild<QWidget *>("_materialEdge",Qt::FindDirectChildrenOnly));
        QVERIFY(item->isChecked());
    }
    void selectedAccentUpdatesOpenControls_data() { normalizationKeepsSemanticFillsAndRemovesDecorativeStrokes_data(); }
    void materialShadesPreserveTheSelectedHue() {
        for(const QColor accent : {QColor("#C04B38"),QColor("#19856D"),QColor("#3165A9"),QColor("#808080")}) {
            for(const bool dark : {false,true}) {
                const auto colors=material_palette(accent,dark);
                for(const QColor shade : {colors.surface,colors.shadow,colors.highlight}) {
                    if(accent.hslSaturationF()>0) QVERIFY(qAbs(shade.hslHueF()-accent.hslHueF())<0.001);
                    QVERIFY(shade.hslSaturationF()<=accent.hslSaturationF());
                }
                QVERIFY(colors.shadow.lightnessF()<colors.surface.lightnessF());
                QVERIFY(colors.highlight.lightnessF()>colors.surface.lightnessF());
                if(!dark) QVERIFY(colors.shadow.lightnessF()>0.8);
            }
        }
    }
    void selectedAccentUpdatesOpenControls() {
        QFETCH(QString,mode);mode_=mode;get_ui_theme()->set_mode(mode);
        const QColor original=get_theme().theme_color();
        const auto restore=qScopeGuard([&]{get_theme().set_theme_color(original);});
        FrostedFramelessDialog gallery;gallery.resize(420,260);
        auto *layout=new QVBoxLayout(&gallery);layout->setContentsMargins(24,46,24,24);layout->setSpacing(18);
        auto *group=new SettingCardGroup("Preferences",&gallery);
        group->addSettingCard(new SettingCard(FluentIcon::Setting,"Appearance","",group));layout->addWidget(group);
        auto *input=new LineEdit;input->setPlaceholderText("Search");layout->addWidget(input);
        auto *button=new PrimaryPushButton("Save");layout->addWidget(button);
        QSignalSpy changed(get_ui_theme(),&UIThemeManager::theme_changed);
        for(const QString color : {QString("#C04B38"),QString("#19856D"),QString("#3165A9")}) {
            get_theme().set_theme_color(QColor(color));
            QCOMPARE(get_ui_theme()->tokens().accent,color.toLower());
            QVERIFY(button->styleSheet().contains(get_ui_theme()->tokens().accent_strong));
            QCOMPARE(get_ui_theme()->tokens().window,material_palette(QColor(color),mode=="dark").surface.name());
            QVERIFY(clipboard::get_theme_manager()->set_theme(mode));
            QCOMPARE(clipboard::get_theme_manager()->get_current_theme().colors.accent_primary,color.toLower());
            const auto &palette=mode=="dark"?dashboard_dark():dashboard_light();
            QCOMPARE(palette.accent,get_ui_theme()->tokens().accent_strong);
            snapshot(gallery,"accent-"+color.mid(1));
        }
        QCOMPARE(changed.size(),3);
    }
    void sharedControlsAndApplicationWindows() {
        QFETCH(QString,mode); mode_=mode; get_ui_theme()->set_mode(mode);
        FrostedFramelessDialog gallery;
        gallery.setWindowTitle("Material controls");gallery.resize(740,570);
        auto *layout=new QVBoxLayout(&gallery);layout->setContentsMargins(22,42,22,22);layout->setSpacing(14);
        auto *group=new SettingCardGroup("Preferences",&gallery);
        auto *toggle=new SwitchSettingCard(FluentIcon::Setting,"Clipboard history","Keep copied items in the local history",group);
        toggle->setChecked(true);group->addSettingCard(toggle);
        auto *card=new SettingCard(FluentIcon::FontSize,"Language","Choose the interface language",group);
        auto *combo=new ComboBox;combo->addItems({"English","Chinese"});card->addControl(combo);group->addSettingCard(card);
        layout->addWidget(group);
        auto *input=new LineEdit; input->setPlaceholderText("Search or enter text");layout->addWidget(input);
        auto *number=new SpinBox;number->setRange(1,100);number->setValue(30);layout->addWidget(number);
        auto *row=new QHBoxLayout;auto *cancel=new PushButton("Cancel");auto *save=new PrimaryPushButton("Save");row->addStretch();row->addWidget(cancel);row->addWidget(save);layout->addLayout(row);
        snapshot(gallery,"controls");
        input->ensurePolished(); QVERIFY(input->findChild<QWidget *>("_materialEdge"));
        QVERIFY(!input->styleSheet().contains("solid #"));
        QSignalSpy clicked(save,&QPushButton::clicked);QTest::mouseClick(save,Qt::LeftButton);QCOMPARE(clicked.size(),1);
        input->setText("working");QCOMPARE(input->text(),QString("working"));
        combo->setCurrentIndex(1);QCOMPARE(combo->currentText(),QString("Chinese"));
        {QMenu menu;menu.addAction("Copy");menu.addSeparator();menu.addAction("Settings");snapshot(menu,"menu");}
        {StandardDialog dialog(nullptr,"Confirmation","Choose an action to continue.");snapshot(dialog,"confirmation");}
        {SettingsDialog dialog(config_);snapshot(dialog,"settings");dialog.on_nav_changed(1);snapshot(dialog,"settings-capture");dialog.on_nav_changed(10);snapshot(dialog,"settings-mouse");}
        {WelcomeWizard wizard(config_);snapshot(wizard,"welcome");}
        {TranslationDialog dialog("Source text","Translated text");dialog.set_theme(mode);snapshot(dialog,"translation");}
        {TranslationPopup popup;popup.set_theme(mode);snapshot(popup,"translation-popup");}
        {ColorPickerDialog dialog(QColor("#6F8FAB"));snapshot(dialog,"color-picker");}
        {QImage image(420,260,QImage::Format_ARGB32);image.fill(QColor("#6F8FAB"));SaveImageDialog dialog(image,"sample",config_);snapshot(dialog,"save-image");}
        {Toolbar toolbar; snapshot(toolbar,"toolbar");}
        {QVERIFY(clipboard::get_theme_manager()->set_theme(mode));clipboard::ClipboardWindow window;window.resize(480,570);snapshot(window,"clipboard");}
    }
};

QTEST_MAIN(MaterialUiTests)
#include "test_material_ui.moc"
