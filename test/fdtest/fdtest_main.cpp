// CAN-FD regression harness for SavvyCAN tool windows.
// Builds against the real sources; opens each tool window with >8 byte frames under ASan/UBSan
// and checks a few observable results. Exit code is the number of failed checks.
#include <QApplication>
#include <QSettings>
#include <QTimer>
#include <QEventLoop>
#include <QListWidget>
#include <QMetaObject>
#include <QDebug>
#include <QDir>
#include <cstdio>

// Test-only hack so the harness can poke private members of the windows under test.
#define private public
#define protected public
#include "mainwindow.h"
#include "connections/canconmanager.h"
#include "framefileio.h"
#include "bus_protocols/isotp_handler.h"
#include "re/frameinfowindow.h"
#include "re/flowviewwindow.h"
#include "re/sniffer/snifferwindow.h"
#include "re/sniffer/sniffermodel.h"
#include "re/filecomparatorwindow.h"
#include "re/rangestatewindow.h"
#include "re/discretestatewindow.h"
#include "re/fuzzingwindow.h"
#include "re/graphingwindow.h"
#include "re/temporalgraphwindow.h"
#include "bisectwindow.h"
#include "frameplaybackwindow.h"
#include "signalviewerwindow.h"
#include "re/isotp_interpreterwindow.h"
#include "canbridgewindow.h"
#include "ui_frameinfowindow.h"
#include "ui_flowviewwindow.h"
#include "ui_snifferwindow.h"
#include "ui_filecomparatorwindow.h"
#include "ui_rangestatewindow.h"
#include "ui_graphingwindow.h"
#undef private
#undef protected

static int failures = 0;
#define CHECK(cond, ...) do { if (cond) { fprintf(stderr, "  PASS: " __VA_ARGS__); } else { failures++; fprintf(stderr, "  FAIL: " __VA_ARGS__); } fprintf(stderr, "\n"); } while (0)
#define SECTION(name) fprintf(stderr, "\n===== %s =====\n", name); fflush(stderr);

static void pump(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

static CANFrame mkFrame(uint32_t id, const QByteArray &payload, uint64_t micros)
{
    CANFrame f;
    f.bus = 0;
    f.isReceived = true;
    f.setFrameId(id);
    f.setExtendedFrameFormat(id > 0x7FF);
    f.setFrameType(QCanBusFrame::DataFrame);
    f.setFlexibleDataRateFormat(payload.size() > 8);
    f.setPayload(payload);
    f.setTimeStamp(QCanBusFrame::TimeStamp(0, micros));
    return f;
}

static QVector<CANFrame> makeFrames(int count, uint64_t startMicros)
{
    struct Spec { uint32_t id; int len; };
    const Spec specs[] = { {0x100, 8}, {0x110, 12}, {0x120, 16}, {0x130, 20}, {0x140, 24},
                           {0x150, 32}, {0x160, 48}, {0x170, 64}, {0x18F00400u, 64} };
    QVector<CANFrame> frames;
    uint64_t t = startMicros;
    for (int i = 0; i < count; i++)
    {
        for (const Spec &s : specs)
        {
            QByteArray p(s.len, 0);
            for (int b = 0; b < s.len; b++) p[b] = static_cast<char>((b * 7 + i * 13 + (s.id & 0xFF)) & 0xFF);
            if (s.len > 8) p[s.len - 1] = static_cast<char>(i & 0xFF);   // last byte always changes
            frames.append(mkFrame(s.id, p, t));
            t += 1000 + (s.id & 0x7F);
        }
    }
    return frames;
}

static void selectAllRows(QWidget *w, const char *listName, int pumpMs = 50)
{
    QListWidget *list = w->findChild<QListWidget *>(listName);
    if (!list) { fprintf(stderr, "  (no list widget %s)\n", listName); return; }
    fprintf(stderr, "  %d ids in %s\n", list->count(), listName);
    for (int i = 0; i < list->count(); i++)
    {
        list->setCurrentRow(i);
        pump(pumpMs);
    }
}

static QByteArray seq(int len, int seed)
{
    QByteArray b(len, 0);
    for (int i = 0; i < len; i++) b[i] = static_cast<char>((seed + i * 3) & 0xFF);
    return b;
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication a(argc, argv);
    a.setOrganizationName("EVTV");
    a.setApplicationName("SavvyCAN");
    a.setOrganizationDomain("evtv.me");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    // keep the harness away from the user's real settings file
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::applicationDirPath() + "/settings");

    SECTION("MainWindow");
    MainWindow *mw = new MainWindow();
    mw->show();
    pump(300);
    CANFrameModel *model = mw->getCANFrameModel();

    QVector<CANFrame> frames = makeFrames(60, 1000000);
    for (const CANFrame &f : frames) model->addFrame(f, false);
    model->sendRefresh();
    pump(400);
    emit mw->framesUpdated(-2);
    pump(200);
    fprintf(stderr, "model has %d frames\n", model->getListReference()->count());
    const QVector<CANFrame> *list = model->getListReference();

    SECTION("FrameInfoWindow (Frame Data Analysis)");
    {
        FrameInfoWindow *w = new FrameInfoWindow(list);
        w->show();
        pump(200);
        selectAllRows(w, "listFrameID", 100);
        CHECK(w->byteGraphsShown == 64, "64 byte plots shown for a 64 byte frame (got %d)", w->byteGraphsShown);
        // back to a classic 8 byte id
        w->ui->listFrameID->setCurrentRow(0);
        pump(100);
        CHECK(w->byteGraphsShown == 8, "8 byte plots shown for an 8 byte frame (got %d)", w->byteGraphsShown);
        QMetaObject::invokeMethod(w, "mouseDoubleClick");
        pump(50);
        w->close();
        delete w;
    }

    SECTION("FlowViewWindow");
    {
        FlowViewWindow *w = new FlowViewWindow(list);
        w->show();
        pump(200);
        selectAllRows(w, "listFrameID", 100);
        CHECK(w->ui->graphView->graphCount() == 64, "64 byte graphs for the 64 byte id (got %d)", w->ui->graphView->graphCount());
        // step through a few frames of the last (64 byte) id
        for (int i = 0; i < 5; i++) { QMetaObject::invokeMethod(w, "btnFwdOneClick"); pump(30); }
        QMetaObject::invokeMethod(w, "btnPlayClick"); pump(400);
        QMetaObject::invokeMethod(w, "btnStopClick"); pump(50);
        // set a trigger bit on byte 40 then play; must not crash
        QMetaObject::invokeMethod(w, "gotCellClick", Q_ARG(int, 40 * 8 + 3)); pump(20);
        QMetaObject::invokeMethod(w, "btnPlayClick"); pump(300);
        QMetaObject::invokeMethod(w, "btnStopClick"); pump(50);
        // live-mode incremental path: append more frames while window is open
        w->ui->cbLiveMode->setChecked(true);
        QVector<CANFrame> more = makeFrames(5, 5000000);
        for (const CANFrame &f : more) model->addFrame(f, false);
        model->sendRefresh();
        emit mw->framesUpdated(more.count());
        pump(300);
        // clear + re-add path (dangling graph references)
        emit mw->framesUpdated(-1);
        pump(100);
        emit mw->framesUpdated(more.count());
        pump(100);
        w->close();
        delete w;
    }

    SECTION("SnifferWindow");
    {
        SnifferWindow *w = new SnifferWindow();
        w->show();
        pump(200);
        w->ui->cbViewBits->setChecked(true); // bit-grid delegate paints every data column
        for (int round = 0; round < 6; round++)
        {
            QVector<CANFrame> batch = makeFrames(1, 9000000 + round * 100000);
            w->mModel.update(nullptr, batch);
            pump(250);
        }
        CHECK(w->mModel.columnCount() == 3 + 64 + 1, "sniffer grew to 64 data columns (columnCount=%d)", w->mModel.columnCount());
        QMetaObject::invokeMethod(&w->mModel, "notch");
        pump(250);
        QMetaObject::invokeMethod(&w->mModel, "unNotch");
        pump(250);
        w->close();
        pump(50);
        CHECK(w->mModel.columnCount() == 3 + 8 + 1, "sniffer resets to 8 data columns on close (columnCount=%d)", w->mModel.columnCount());
        delete w;
    }

    SECTION("FileComparatorWindow");
    {
        FileComparatorWindow *w = new FileComparatorWindow();
        w->show();
        pump(100);
        w->interestedFrames = makeFrames(20, 1000000);
        w->referenceFrames = makeFrames(20, 2000000);
        // make the reference side differ: extra id and a shorter version of the 64 byte id
        w->referenceFrames.append(mkFrame(0x555, seq(24, 1), 3000000));
        w->referenceFrames.append(mkFrame(0x170, seq(12, 9), 3000100));
        w->interestedFilename = "fd_interested";
        w->calculateDetails();
        pump(200);
        CHECK(w->ui->treeDetails->topLevelItemCount() == 3, "comparison tree populated (%d top level items)", w->ui->treeDetails->topLevelItemCount());
        w->ui->ckUniqueToInterested->setChecked(true);
        w->calculateDetails();
        pump(200);
        w->referenceFrames.clear();
        w->calculateDetails();
        pump(200);
        w->close();
        delete w;
    }

    SECTION("ISO-TP handler with CAN-FD framing");
    {
        ISOTP_HANDLER handler;
        handler.setProcessAll(true);
        QList<ISOTP_MESSAGE> got;
        QObject::connect(&handler, &ISOTP_HANDLER::newISOMessage, [&got](ISOTP_MESSAGE m) { got.append(m); });

        QVector<CANFrame> v;
        uint64_t t = 100;
        // 1. classic single frame
        v.append(mkFrame(0x7E8, QByteArray::fromHex("0362F19000000000"), t++));
        // 2. CAN-FD single frame escape: 00 <len> data...
        QByteArray sf = QByteArray::fromHex("000A") + seq(10, 0x40) + QByteArray(4, 0);
        v.append(mkFrame(0x7E9, sf, t++));
        // 3. classic multi frame, 20 bytes
        QByteArray m3 = seq(20, 0x60);
        v.append(mkFrame(0x7EA, QByteArray::fromHex("1014") + m3.mid(0, 6), t++));
        v.append(mkFrame(0x7EA, QByteArray::fromHex("21") + m3.mid(6, 7), t++));
        v.append(mkFrame(0x7EA, QByteArray::fromHex("22") + m3.mid(13, 7), t++));
        // 4. CAN-FD multi frame with 12 bit length (100 bytes): 64 byte first frame, 48 byte consecutive frame
        QByteArray m4 = seq(100, 0x80);
        v.append(mkFrame(0x7EB, QByteArray::fromHex("1064") + m4.mid(0, 62), t++));
        v.append(mkFrame(0x7EB, QByteArray::fromHex("21") + m4.mid(62, 38) + QByteArray(9, 0), t++));
        // 5. CAN-FD multi frame with 32 bit length escape (256 bytes)
        QByteArray m5 = seq(256, 0xA0);
        v.append(mkFrame(0x7EC, QByteArray::fromHex("100000000100") + m5.mid(0, 58), t++));
        int pos = 58, sn = 1;
        while (pos < 256)
        {
            int n = qMin(63, 256 - pos);
            QByteArray cf; cf.append(static_cast<char>(0x20 | (sn & 0xF))); cf.append(m5.mid(pos, n));
            if (cf.size() < 64 && n == 63) cf.append(QByteArray(64 - cf.size(), 0));
            v.append(mkFrame(0x7EC, cf, t++));
            pos += n; sn++;
        }
        // 6. junk that must be ignored without crashing
        v.append(mkFrame(0x7ED, QByteArray(), t++));                       // empty payload
        v.append(mkFrame(0x7ED, QByteArray::fromHex("10"), t++));           // truncated first frame
        v.append(mkFrame(0x7ED, QByteArray::fromHex("003F") + seq(10, 1), t++)); // FD escape claiming more than present
        v.append(mkFrame(0x7ED, QByteArray::fromHex("3000"), t++));         // flow control missing separation time

        handler.rapidFrames(nullptr, v);
        pump(50);

        CHECK(got.count() == 5, "decoded 5 ISO-TP messages (got %d)", got.count());
        auto check = [&](int idx, uint32_t id, const QByteArray &expect)
        {
            bool ok = idx < got.count() && got[idx].frameId() == id && got[idx].payload() == expect;
            CHECK(ok, "message %d id 0x%X len %d decoded correctly%s", idx, id, expect.size(),
                  ok ? "" : (idx < got.count() ? QString(" (got id 0x%1 len %2)").arg(got[idx].frameId(), 0, 16).arg(got[idx].payload().size()).toUtf8().constData() : " (missing)"));
        };
        check(0, 0x7E8, QByteArray::fromHex("62F190"));
        check(1, 0x7E9, seq(10, 0x40));
        check(2, 0x7EA, m3);
        check(3, 0x7EB, m4);
        check(4, 0x7EC, m5);
    }

    SECTION("Native CSV round trip with CAN-FD frames");
    {
        QString path = QCoreApplication::applicationDirPath() + "/fd_roundtrip.csv";
        QVector<CANFrame> out = makeFrames(3, 1000000);
        CHECK(FrameFileIO::saveNativeCSVFile(path, &out), "saved native CSV");
        CHECK(FrameFileIO::isNativeCSVFile(path), "file recognised as native CSV");
        QVector<CANFrame> in;
        CHECK(FrameFileIO::loadNativeCSVFile(path, &in), "loaded native CSV");
        bool same = in.count() == out.count();
        for (int i = 0; same && i < in.count(); i++)
            same = in[i].frameId() == out[i].frameId() && in[i].payload() == out[i].payload();
        CHECK(same, "all %d frames (up to 64 bytes) survived the round trip (loaded %d)", out.count(), in.count());
        QVector<CANFrame> auto_;
        CHECK(FrameFileIO::autoDetectLoadFile(path, &auto_) && auto_.count() == out.count(), "auto detect load works (%d frames)", auto_.count());
    }

    SECTION("GraphingWindow with signals beyond bit 64");
    {
        GraphingWindow *g = new GraphingWindow(list);
        g->show();
        pump(200);
        GraphParams gp;
        gp.ID = 0x170; gp.startBit = 496; gp.numBits = 16; gp.intelFormat = true; gp.graphName = "fd tail";
        g->createGraph(gp);
        pump(150);
        GraphParams gp2;
        gp2.ID = 0x18F00400u; gp2.startBit = 200; gp2.numBits = 32; gp2.intelFormat = false; gp2.isSigned = true; gp2.graphName = "fd big endian";
        g->createGraph(gp2);
        pump(150);
        QVector<CANFrame> more = makeFrames(5, 7000000);
        for (const CANFrame &f : more) model->addFrame(f, false);
        model->sendRefresh();
        emit mw->framesUpdated(more.count());
        pump(300);
        CHECK(g->ui->graphingView->graphCount() == 2, "two graphs present (got %d)", g->ui->graphingView->graphCount());
        g->close();
        delete g;
    }

    SECTION("RangeStateWindow recalc over 64 byte frames");
    {
        RangeStateWindow *r = new RangeStateWindow(list);
        r->show();
        pump(200);
        r->ui->spinMaxSigSize->setValue(16);
        r->ui->spinMinSigSize->setValue(8);
        r->ui->spinGranularity->setValue(8);
        QMetaObject::invokeMethod(r, "recalcButton");
        pump(200);
        fprintf(stderr, "  range candidates found: %d\n", r->ui->listCandidates->count());
        if (r->ui->listCandidates->count() > 0) { r->ui->listCandidates->setCurrentRow(r->ui->listCandidates->count() - 1); pump(100); }
        r->close();
        delete r;
    }

    SECTION("Other windows: open, refresh, close");
    {
        QList<QDialog *> wins;
        wins << new DiscreteStateWindow(list) << new FuzzingWindow(list)
             << new TemporalGraphWindow(list) << new BisectWindow(list)
             << new FramePlaybackWindow(list) << new SignalViewerWindow(list)
             << new ISOTP_InterpreterWindow(list) << new CANBridgeWindow(list);
        for (QDialog *w : wins)
        {
            fprintf(stderr, "  -- %s\n", w->metaObject()->className()); fflush(stderr);
            w->show();
            pump(200);
            emit mw->framesUpdated(-2);
            pump(200);
            selectAllRows(w, "listFrameID", 50);
            if (QString(w->metaObject()->className()) == "FuzzingWindow")
            {
                QMetaObject::invokeMethod(w, "changedNumDataBytes", Q_ARG(int, 64));
                pump(100);
            }
            w->close();
            pump(50);
            delete w;
        }
    }

    SECTION("DONE");
    fprintf(stderr, "%d check(s) failed\n", failures);
    mw->close();
    delete mw;
    return failures;
}
