#include "filecomparatorwindow.h"
#include "ui_filecomparatorwindow.h"
#include "helpwindow.h"
#include <QProgressDialog>
#include <QSettings>
#include <qevent.h>

FileComparatorWindow::FileComparatorWindow(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::FileComparatorWindow)
{
    ui->setupUi(this);
    setWindowFlags(Qt::Window);

    connect(ui->btnInterestedFile, SIGNAL(clicked(bool)), this, SLOT(loadInterestedFile()));
    connect(ui->btnLoadRefFile, SIGNAL(clicked(bool)), this, SLOT(loadReferenceFile()));
    connect(ui->btnSaveDetails, SIGNAL(clicked(bool)), this, SLOT(saveDetails()));
    connect(ui->btnClear, SIGNAL(clicked(bool)), this, SLOT(clearReference()));

    ui->lblFirstFile->setText("");
    ui->lblRefFrames->setText("Loaded frames: 0");

    dbcHandler = DBCHandler::getReference();

    installEventFilter(this);
}

FileComparatorWindow::~FileComparatorWindow()
{
    removeEventFilter(this);
    delete ui;
}

void FileComparatorWindow::showEvent(QShowEvent *)
{
    readSettings();
}

void FileComparatorWindow::closeEvent(QCloseEvent *event)
{
    Q_UNUSED(event)
    writeSettings();
}

bool FileComparatorWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyRelease) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        switch (keyEvent->key())
        {
        case Qt::Key_F1:
            HelpWindow::getRef()->showHelp("filecomparison.md");
            break;
        }
        return true;
    } else {
        // standard event processing
        return QObject::eventFilter(obj, event);
    }
    return false;
}

void FileComparatorWindow::readSettings()
{
    QSettings settings;
    if (settings.value("Main/SaveRestorePositions", false).toBool())
    {
        resize(settings.value("FileComparator/WindowSize", QSize(720, 631)).toSize());
        move(Utility::constrainedWindowPos(settings.value("FileComparator/WindowPos", QPoint(50, 50)).toPoint()));
    }
}

void FileComparatorWindow::writeSettings()
{
    QSettings settings;

    if (settings.value("Main/SaveRestorePositions", false).toBool())
    {
        settings.setValue("FileComparator/WindowSize", size());
        settings.setValue("FileComparator/WindowPos", pos());
    }
}

void FileComparatorWindow::loadInterestedFile()
{
    interestedFrames.clear();
    QString resultingFileName;

    qApp->processEvents();

    if (FrameFileIO::loadFrameFile(resultingFileName, &interestedFrames))
    {
        ui->lblFirstFile->setText(resultingFileName);
        interestedFilename = resultingFileName;
        if (interestedFrames.count() > 0 && referenceFrames.count() > 0) calculateDetails();
    }

}

void FileComparatorWindow::loadReferenceFile()
{
    //secondFileFrames.clear();
    QString resultingFileName;

    qApp->processEvents();

    if (FrameFileIO::loadFrameFile(resultingFileName, &referenceFrames))
    {
        ui->lblRefFrames->setText("Loaded frames: " + QString::number(referenceFrames.length()));
        if (interestedFrames.count() > 0 && referenceFrames.count() > 0) calculateDetails();
    }
}

void FileComparatorWindow::clearReference()
{
    referenceFrames.clear();
    ui->treeDetails->clear();
    ui->lblRefFrames->setText("Loaded frames: " + QString::number(referenceFrames.length()));
}

//Accumulate per-ID statistics (byte value histograms, bits ever set, DBC signal values) for a set of frames.
//Handles any payload length up to the CAN-FD maximum of 64 bytes.
static void accumulateFrames(const QVector<CANFrame> &frames, QMap<uint32_t, FrameData> &ids, DBCHandler *dbcHandler)
{
    int counter = 0;
    for (const CANFrame &frame : frames)
    {
        counter++;
        if (counter > 200)
        {
            counter = 0;
            qApp->processEvents();
        }

        const unsigned char *data = reinterpret_cast<const unsigned char *>(frame.payload().constData());
        int dataLen = qMin(frame.payload().count(), FrameData::maxDataBytes);

        FrameData &fd = ids[frame.frameId()]; //creates a blank entry the first time this ID is seen
        fd.ID = frame.frameId();
        fd.ensureLength(dataLen);
        for (int y = 0; y < dataLen; y++)
        {
            fd.values[y][data[y]]++;
            fd.bitmap[y] |= data[y];
        }

        DBC_MESSAGE *msg = dbcHandler->findMessage(frame.frameId());
        if (msg)
        {
            int numSignals = msg->sigHandler->getCount();
            for (int i = 0; i < numSignals; i++)
            {
                DBC_SIGNAL *sig = msg->sigHandler->findSignalByIdx(i);
                if (sig && sig->isSignalInMessage(frame))
                {
                    QString sigVal;
                    if (sig->processAsText(frame, sigVal, false))
                    {
                        QList<QString> &vals = fd.signalInstances[sig->name];
                        if (!vals.contains(sigVal)) vals.append(sigVal);
                    }
                }
            }
        }
    }
}

void FileComparatorWindow::calculateDetails()
{
    QMap<uint32_t, FrameData> interestedIDs;
    QMap<uint32_t, FrameData> referenceIDs;
    QTreeWidgetItem *interestedOnlyBase, *referenceOnlyBase = nullptr, *sharedBase, *bitmapBaseInterested, *bitmapBaseReference = nullptr;
    QTreeWidgetItem *valuesBase, *detail, *sharedItem, *valuesInterested, *valuesReference = nullptr;

    bool uniqueInterested = ui->ckUniqueToInterested->isChecked();

    QProgressDialog progress(this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setLabelText("Calculating differences");
    progress.setCancelButton(nullptr);
    progress.setRange(0,0);
    progress.setMinimumDuration(0);
    progress.show();

    qApp->processEvents();

    ui->treeDetails->clear();

    interestedOnlyBase = new QTreeWidgetItem();
    interestedOnlyBase->setText(0,"IDs found only in " + interestedFilename);
    if (!uniqueInterested)
    {
        referenceOnlyBase = new QTreeWidgetItem();
        referenceOnlyBase->setText(0, "IDs found only in Side 2 - Reference frames");
    }
    sharedBase = new QTreeWidgetItem();
    sharedBase->setText(0,"IDs found on both sides");

    //first we have to fill out the data structures to get ready to do the report
    accumulateFrames(interestedFrames, interestedIDs, dbcHandler);
    qApp->processEvents();
    accumulateFrames(referenceFrames, referenceIDs, dbcHandler);
    qApp->processEvents();

    //now we iterate through the IDs within both files and see which are unique to one file and which
    //are shared
    bool interestedHadUnique = false;
    QMap<uint32_t, FrameData>::iterator i;
    int framesCounter = 0;
    for (i = interestedIDs.begin(); i != interestedIDs.end(); ++i)
    {
        framesCounter++;
        if (framesCounter > 50)
        {
            framesCounter = 0;
            qApp->processEvents();
        }

        uint32_t keyone = i.key();
        if (!referenceIDs.contains(keyone))
        {
            valuesBase = new QTreeWidgetItem();
            DBC_MESSAGE *msg = dbcHandler->findMessage(keyone);
            if (msg)
            {
                valuesBase->setText(0, Utility::formatHexNum(keyone) + " (" + msg->name + ")");
            }
            else valuesBase->setText(0, Utility::formatHexNum(keyone));
            interestedOnlyBase->addChild(valuesBase);
        }
        else //ID was in both files
        {
            interestedHadUnique = false;
            sharedItem = new QTreeWidgetItem();
            DBC_MESSAGE *msg = dbcHandler->findMessage(keyone);
            if (msg)
            {
                sharedItem->setText(0, Utility::formatHexNum(keyone) + " (" + msg->name + ")");
            }
            else sharedItem->setText(0, Utility::formatHexNum(keyone));
            //if the ID was in both files then we can use the data accumulated above in bitmap
            //and values to figure out what has changed between the two files

            const FrameData &interested = i.value();
            const FrameData &reference = referenceIDs[keyone];
            int longestLen = qMax(interested.dataLen, reference.dataLen);

            bitmapBaseInterested = new QTreeWidgetItem();
            bitmapBaseInterested->setText(0, "Bits set only in " + interestedFilename);
            if (!uniqueInterested)
            {
                bitmapBaseReference = new QTreeWidgetItem();
                bitmapBaseReference->setText(0, "Bits set only in Side 2 - Reference frames");
            }
            sharedItem->addChild(bitmapBaseInterested);
            if (!uniqueInterested) sharedItem->addChild(bitmapBaseReference);

            //first up, which bits were set in one file but not the other
            for (int b = 0; b < (8 * longestLen); b++)
            {
                bool inInterested = interested.bitSet(b);
                bool inReference = reference.bitSet(b);
                if (inInterested == inReference) continue;
                if (!inInterested && uniqueInterested) continue; //only reporting things unique to the interested side

                detail = new QTreeWidgetItem();
                detail->setText(0, QString::number(b) + " (" + QString::number(b / 8) + ":" + QString::number(b % 8) + ")");
                if (inInterested)
                {
                    bitmapBaseInterested->addChild(detail);
                    interestedHadUnique = true;
                }
                else bitmapBaseReference->addChild(detail);
            }

            for (int byt = 0; byt < longestLen; byt++)
            {
                valuesBase = new QTreeWidgetItem();
                valuesBase->setText(0, "Byte " + QString::number(byt));
                sharedItem->addChild(valuesBase);
                valuesInterested = new QTreeWidgetItem();
                valuesInterested->setText(0, "Values found only in " + interestedFilename);
                if (!uniqueInterested)
                {
                    valuesReference = new QTreeWidgetItem();
                    valuesReference->setText(0, "Values found only in Side 2 - Reference frames");
                }
                valuesBase->addChild(valuesInterested);
                if (!uniqueInterested) valuesBase->addChild(valuesReference);
                for (int j = 0; j < 256; j++)
                {
                    int interestedCount = interested.valueCount(byt, j);
                    int referenceCount = reference.valueCount(byt, j);
                    if ((interestedCount > 0) && (referenceCount == 0) )
                    {
                        detail = new QTreeWidgetItem();
                        detail->setText(0, Utility::formatHexNum(static_cast<unsigned int>(j)));
                        valuesInterested->addChild(detail);
                        interestedHadUnique = true;
                    }
                    if ((referenceCount > 0) && (interestedCount == 0) && !uniqueInterested)
                    {
                        detail = new QTreeWidgetItem();
                        detail->setText(0, Utility::formatHexNum(static_cast<unsigned int>(j)));
                        valuesReference->addChild(detail);
                    }
                }
            }

            //presumably both include the same signals so for this first attempt just
            //take all signals from the reference and then find that same signal in
            //the interested frames and then see what unique values there were in either one

            QHash<QString, QList<QString>>::const_iterator it = reference.signalInstances.constBegin();
            while (it != reference.signalInstances.constEnd())
            {
                valuesBase = new QTreeWidgetItem();
                valuesBase->setText(0, "Signal " + it.key());
                sharedItem->addChild(valuesBase);
                valuesInterested = new QTreeWidgetItem();
                valuesInterested->setText(0, "Values found only in " + interestedFilename);
                if (!uniqueInterested)
                {
                    valuesReference = new QTreeWidgetItem();
                    valuesReference->setText(0, "Values found only in Side 2 - Reference frames");
                }
                valuesBase->addChild(valuesInterested);
                if (!uniqueInterested) valuesBase->addChild(valuesReference);

                QList<QString> refVals = it.value();
                QList<QString> interestedVals = interested.signalInstances[it.key()];
                foreach (QString str, refVals)
                {
                    if (!interestedVals.contains(str))
                    {
                        qDebug() << "Interested frames didn't contain value: " << str << " in signal " << it.key();
                        detail = new QTreeWidgetItem();
                        detail->setText(0, str);
                        valuesReference->addChild(detail);
                    }
                }
                qApp->processEvents();
                foreach (QString str, interestedVals)
                {
                    if (!refVals.contains(str))
                    {
                        qDebug() << "Reference frames didn't contain value: " << str << " in signal " << it.key();
                        detail = new QTreeWidgetItem();
                        detail->setText(0, str);
                        valuesInterested->addChild(detail);
                    }
                }
                ++it;
            }

            if (interestedHadUnique || !uniqueInterested) sharedBase->addChild(sharedItem);
        }
    }

    qApp->processEvents();

    if (!uniqueInterested)
    {
        QMap<uint32_t, FrameData>::iterator itwo;
        for (itwo = referenceIDs.begin(); itwo != referenceIDs.end(); ++itwo)
        {
            unsigned int keytwo = itwo.key();
            if (!interestedIDs.contains(keytwo))
            {
                valuesBase = new QTreeWidgetItem();
                DBC_MESSAGE *msg = dbcHandler->findMessage(keytwo);
                if (msg)
                {
                    valuesBase->setText(0, Utility::formatHexNum(keytwo) + " (" + msg->name + ")" );
                }
                else valuesBase->setText(0, Utility::formatHexNum(keytwo));
                referenceOnlyBase->addChild(valuesBase);
            }
        }
    }

    ui->treeDetails->addTopLevelItem(interestedOnlyBase);
    if (!uniqueInterested) ui->treeDetails->addTopLevelItem(referenceOnlyBase);
    ui->treeDetails->addTopLevelItem(sharedBase);

    //ui->treeDetails->setSortingEnabled(true);
    //ui->treeDetails->sortByColumn(0, Qt::AscendingOrder);

    QSettings settings;
    if (settings.value("InfoCompare/AutoExpand", false).toBool())
    {
        ui->treeDetails->expandAll();
    }

    progress.cancel();
}

void FileComparatorWindow::saveDetails()
{
    QString filename;
    QFileDialog dialog(this);
    QSettings settings;

    QStringList filters;
    filters.append(QString(tr("Text File (*.txt)")));

    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilters(filters);
    dialog.setViewMode(QFileDialog::Detail);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setDirectory(settings.value("FileComparator/LoadSaveDirectory", dialog.directory().path()).toString());

    if (dialog.exec() == QDialog::Accepted)
    {
        filename = dialog.selectedFiles()[0];
        settings.setValue("FileComparator/LoadSaveDirectory", dialog.directory().path());
        if (!filename.contains('.')) filename += ".txt";
        QFile *outFile = new QFile(filename);

        if (!outFile->open(QIODevice::WriteOnly | QIODevice::Text))
            return;

        QTreeWidget *tree = ui->treeDetails;


        QTreeWidgetItemIterator it(tree);
        while (*it) {
          QTreeWidgetItem *item = *it;
          QString itemText = item->text(0);
          while (item->parent())
          {
              outFile->write("   ");
              item = item->parent();
          }
          outFile->write(itemText.toUtf8() + "\n");
          ++it;
        }

        outFile->close();

    }
}

