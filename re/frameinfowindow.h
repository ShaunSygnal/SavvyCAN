#ifndef FRAMEINFOWINDOW_H
#define FRAMEINFOWINDOW_H

#include <QDialog>
#include <QFile>
#include <QListWidget>
#include <QTreeWidget>
#include <candatagrid.h>
#include "can_structs.h"
#include "bus_protocols/j1939_handler.h"
#include "dbc/dbchandler.h"

#include "qcustomplot.h"

namespace Ui {
class FrameInfoWindow;
}

class FrameInfoWindow : public QDialog
{
    Q_OBJECT

public:
    explicit FrameInfoWindow(const QVector<CANFrame> *frames, QWidget *parent = 0);
    ~FrameInfoWindow();
    void showEvent(QShowEvent*);

private slots:
    void updateDetailsWindow(QString);
    void updatedFrames(int);
    void saveDetails();
    void mousePress();
    void mouseWheel();
    void mouseDoubleClick();

private:
    Ui::FrameInfoWindow *ui;
    QVector<QCustomPlot *> graphByte; //one plot per data byte, created on demand (up to 64 for CAN-FD)
    int byteGraphsShown;
    QCustomPlot *graphHistogram;
    CANDataGrid *heatmap;

    QList<int> foundID;
    QList<CANFrame> frameCache;
    const QVector<CANFrame> *modelFrames;
    bool useOpenGL;
    bool useHexTicker;
    static const QColor byteGraphColors[8];
    static QPen bytePens[8];
    DBCHandler *dbcHandler;


    void refreshIDList();
    void closeEvent(QCloseEvent *event);
    bool eventFilter(QObject *obj, QEvent *event);
    void setupByteGraph(QCustomPlot *plot, int num);
    void ensureByteGraphs(int count);
    void readSettings();
    void writeSettings();
    void dumpNode(QTreeWidgetItem* item, QFile *file, int indent);
};

#endif // FRAMEINFOWINDOW_H
