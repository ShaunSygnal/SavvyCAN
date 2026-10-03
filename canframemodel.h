#ifndef CANFRAMEMODEL_H
#define CANFRAMEMODEL_H

#include <QAbstractTableModel>
#include <QList>
#include <QVector>
#include <QDebug>
#include <QMutex>
#include <QHash>
#include <QByteArray>
#include "can_structs.h"
#include "dbc/dbchandler.h"
#include "connections/canconnection.h"
#include "utility.h"

enum class Column {
    TimeStamp = 0, ///< The timestamp when the frame was transmitted or received
    FrameId   = 1, ///< The frames CAN identifier (Standard: 11 or Extended: 29 bit)
    Extended  = 2, ///< True if the frames CAN identifier is 29 bit
    Remote    = 3, ///< True if the frames is a remote frame
    Direction = 4, ///< Whether the frame was transmitted or received
    Bus       = 5, ///< The bus where the frame was transmitted or received
    Length    = 6, ///< The frames payload data length
    ASCII     = 7, ///< The payload interpreted as ASCII characters
    Data      = 8, ///< The frames payload data
    NUM_COLUMN
};

class CANFrameModel: public QAbstractTableModel
{
    Q_OBJECT

public:
    CANFrameModel(QObject *parent = 0);
    virtual ~CANFrameModel();

    int rowCount(const QModelIndex &parent = QModelIndex()) const;
    QVariant data(const QModelIndex &index, int role) const;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const;
    int columnCount(const QModelIndex &) const;
    int totalFrameCount();

    void sendRefresh();
    void sendRefresh(int);
    int  sendBulkRefresh();
    void clearFrames();
    void setInterpretMode(bool);
    bool getInterpretMode();
    void setOverwriteMode(bool);
    void setHexMode(bool);
    void setUseColorsByCanId(bool);
    void setClearMode(bool mode);
    void setTimeStyle(TimeStyle newStyle);
    void setIgnoreDBCColors(bool mode);
    void setFilterState(unsigned int ID, bool state);
    void setBusFilterState(unsigned int BusID, bool state);
    void setAllFilters(bool state);
    void setTimeFormat(QString);
    void setBytesPerLine(int bpl);
    void loadFilterFile(QString filename);
    void saveFilterFile(QString filename);
    void normalizeTiming();
    void recalcOverwrite();
    void invalidateMuxSections(); //call when the loaded DBC files change so the per-section frame cache is rebuilt
    bool needsFilterRefresh();
    void insertFrames(const QVector<CANFrame> &newFrames);
    void sortByColumn(int column);
    int getIndexFromTimeID(unsigned int ID, double timestamp);
    const QVector<CANFrame> *getListReference() const; //thou shalt not modify these frames externally!
    const QVector<CANFrame> *getFilteredListReference() const; //Thus saith the Lord, NO.
    const QMap<int, bool> *getFiltersReference() const; //this neither
    const QMap<int, bool> *getBusFiltersReference() const; //this neither

public slots:
    void addFrame(const CANFrame&, bool);
    void addFrames(const CANConnection*, const QVector<CANFrame>&);

signals:
    void updatedFiltersList();

private:
    void qSortCANFrameAsc(QVector<CANFrame>* frames, Column column, int lowerBound, int upperBound);
    void qSortCANFrameDesc(QVector<CANFrame>* frames, Column column, int lowerBound, int upperBound);
    uint64_t getCANFrameVal(QVector<CANFrame> *frames, int row, Column col);
    bool any_filters_are_configured(void);
    bool any_busfilters_are_configured(void);

    //Overwrite mode keeps only the newest frame per ID. For multiplexed messages the other sections would be lost,
    //so we also remember the newest frame for every distinct combination of active multiplexor values, per ID+bus.
    QHash<uint64_t, QHash<QByteArray, CANFrame>> muxSectionFrames;
    bool muxSectionsDirty; //cache needs a rebuild from 'frames' before it can be trusted
    static uint64_t muxKey(const CANFrame &frame);
    static QByteArray muxSignature(DBC_MESSAGE *msg, const CANFrame &frame);
    void storeMuxSection(const CANFrame &frame, DBC_MESSAGE *msg);
    void updateMuxSection(const CANFrame &frame);
    void rebuildMuxSections();
    QString olderSectionText(DBC_SIGNAL *sig, const CANFrame &latest) const;

    QVector<CANFrame> frames;
    QVector<CANFrame> filteredFrames;
    QMap<int, bool> filters;
    QMap<int, bool> busFilters;
    DBCHandler *dbcHandler;
    QMutex mutex;
    bool interpretFrames; //should we use the dbcHandler?
    bool overwriteDups; //should we display all frames or only the newest for each ID?
    bool filtersPersistDuringClear;
    QString timeFormat;
    TimeStyle timeStyle;
    bool useHexMode;
    bool useColorsByCanId;
    bool needFilterRefresh;
    bool ignoreDBCColors;
    int64_t timeOffset;
    int lastUpdateNumFrames;
    uint32_t preallocSize;
    bool sortDirAsc;
    int bytesPerLine;
};


#endif // CANFRAMEMODEL_H

