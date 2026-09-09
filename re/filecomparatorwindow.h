#ifndef FILECOMPARATORWINDOW_H
#define FILECOMPARATORWINDOW_H

#include <QDialog>
#include <QDebug>
#include <QTreeWidget>
#include <array>
#include <vector>
#include "framefileio.h"
#include "can_structs.h"
#include "utility.h"
#include "dbc/dbchandler.h"

namespace Ui {
class FileComparatorWindow;
}

//Per-ID statistics gathered over a set of frames. Sized for anything up to a full 64 byte CAN-FD payload.
struct FrameData
{
    static constexpr int maxDataBytes = 64;

    uint32_t ID = 0;
    int dataLen = 0; //longest payload seen for this ID
    std::array<uint8_t, maxDataBytes> bitmap{}; //per data byte, which bits were ever set
    std::vector<std::array<int, 256>> values; //values[byte][value] = # of times we saw that value in that byte
    QHash<QString, QList<QString>> signalInstances;

    void ensureLength(int len)
    {
        if (len > static_cast<int>(values.size())) values.resize(len, std::array<int, 256>{});
        if (len > dataLen) dataLen = len;
    }

    int valueCount(int byte, int value) const
    {
        if (byte < 0 || byte >= static_cast<int>(values.size())) return 0;
        return values[byte][value & 0xFF];
    }

    bool bitSet(int bit) const
    {
        int byte = bit / 8;
        if (bit < 0 || byte >= maxDataBytes) return false;
        return (bitmap[byte] & (1 << (bit % 8))) != 0;
    }
};

class FileComparatorWindow : public QDialog
{
    Q_OBJECT

public:
    explicit FileComparatorWindow(QWidget *parent = 0);
    ~FileComparatorWindow();

private slots:
    void loadInterestedFile();
    void loadReferenceFile();
    void clearReference();
    void saveDetails();

private:
    Ui::FileComparatorWindow *ui;
    QVector<CANFrame> interestedFrames;
    QVector<CANFrame> referenceFrames;
    QString interestedFilename;
    DBCHandler *dbcHandler;

    void calculateDetails();
    void showEvent(QShowEvent *);
    void closeEvent(QCloseEvent *event);
    bool eventFilter(QObject *obj, QEvent *event);
    void readSettings();
    void writeSettings();
};

#endif // FILECOMPARATORWINDOW_H
