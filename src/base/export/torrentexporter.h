#ifndef TORRENTEXPORTER_H
#define TORRENTEXPORTER_H

#include <QDateTime>
#include <QObject>
#include <QtSql/QSqlDatabase>

#include "base/bittorrent/infohash.h"
#include "base/export/torrentexporterconfig.h"

namespace BitTorrent
{
    class TorrentHandle;
}

namespace Export
{
    class ExporterError final : public std::logic_error
    {
    public:
        explicit inline ExporterError(const char *Message)
            : std::logic_error(Message)
        {}
    };

    // Starts from 1 because of MySQL enums starts from 1
    enum struct TorrentStatus
    {
        Allocating = 1,
        Checking,
        CheckingResumeData,
        Downloading,
        Error,
        Finished,
        ForcedDownloading,
        MissingFiles,
        Moving,
        Paused,
        Queued,
        Stalled,
        Unknown,
    };

    class TorrentExporter final : public QObject
    {
        Q_OBJECT
        Q_DISABLE_COPY(TorrentExporter)

    public:
        typedef quint64 TorrentId;
        typedef QHash<TorrentId, const BitTorrent::TorrentHandle *> TorrentHandleByIdHash;
        typedef QHash<BitTorrent::InfoHash, const BitTorrent::TorrentHandle *>
                TorrentHandleByInfoHashHash;

        static void initInstance();
        static void freeInstance();
        static TorrentExporter *instance();

        inline void setQMediaHwnd(const HWND hwnd) noexcept
        { m_qMediaHwnd = hwnd; };
        inline void setQMediaWindowActive(const bool active) noexcept
        { m_qMediaWindowActive = active; };

    private:
        typedef quint64 TorrentFileId;
        typedef QHash<TorrentId, QSqlRecord> TorrentSqlRecordByIdHash;
        typedef qint32 TorrentFileIndex;
        typedef QHash<TorrentId, QSharedPointer<QHash<TorrentFileIndex, QSqlRecord>>>
                TorrentFileSqlRecordByIdHash;
        typedef QVariantHash TorrentChangedProperties;
        typedef QHash<TorrentId, QSharedPointer<const TorrentChangedProperties>>
                TorrentsChangedHash;
        typedef QVariantHash TorrentFileChangedProperties;
        typedef QHash<TorrentFileId, QSharedPointer<const TorrentFileChangedProperties>>
                TorrentFilesChangedHash;
        typedef QHash<TorrentId, QSharedPointer<const TorrentFilesChangedHash>>
                TorrentsFilesChangedHash;

        static void showDbDisconnected();
        static void showDbConnected();
        /*! Check database connection and show warnings when the state changed. */
        static bool pingDatabase(QSqlDatabase &db);

        static TorrentExporter *m_instance;
        static bool m_dbDisconnectedShowed;
        static bool m_dbConnectedShowed;
        static const int COMMIT_INTERVAL_BASE = 1000;
        /*! Maximum interval between connect attempts to db. */
        static const int COMMIT_INTERVAL_MAX  = 5000;

        TorrentExporter();
        ~TorrentExporter() override;

        void connectDatabase() const;
        void removeTorrentFromDb(const BitTorrent::InfoHash &infoHash) const;
        void insertTorrentsToDb() const;
        /*! Remove already existing torrents in DB from commit hash. */
        void removeExistingTorrents();
        void insertPreviewableFilesToDb() const;
        inline void deferCommitTimerTimeout() const
        {
            m_dbCommitTimer->start(std::min(m_dbCommitTimer->interval() * 2,
                                            COMMIT_INTERVAL_MAX));
        }
        /*! Select inserted torrent ids by InfoHash-es for a torrents to commit and return
            torrent handles mapped by torrent ids. Used only during torrent added alert. */
        TorrentHandleByIdHash
        selectTorrentIdsToCommitByHashes(const QList<BitTorrent::InfoHash> &hashes) const;
        TorrentHandleByIdHash
        mapTorrentHandleById(const TorrentHandleByInfoHashHash &torrents) const;
        std::tuple<const TorrentHandleByIdHash, const TorrentSqlRecordByIdHash>
        selectTorrentsByHandles(
                const TorrentHandleByInfoHashHash &torrents,
                const QString &select = "id, hash") const;
        TorrentExporter::TorrentFileSqlRecordByIdHash
        selectTorrentsFilesByHandles(const TorrentHandleByIdHash &torrentsUpdated) const;
        QHash<TorrentId, BitTorrent::InfoHash>
        selectTorrentsByStatuses(const QList<TorrentStatus> &statuses) const;
        /*! Needed when qBittorrent is closed, to fix torrent downloading statuses. */
        void correctTorrentStatusesOnExit() const;
        /*! Needed when qBittorrent is closed, to set seeds, total_seeds, leechers and
            total_leechers to 0. */
        void correctTorrentPeersOnExit() const;
        /*! Update torrent storage location in DB, after torrent was moved ( storage path
            changed ). */
        void updateTorrentSaveDirInDb(TorrentId torrentId, const QString &newPath,
                                      const QString &torrentName) const;
        bool fillTorrentsChangedProperties(
                const TorrentHandleByInfoHashHash &torrents,
                TorrentsChangedHash &torrentsChangedProperties,
                TorrentsFilesChangedHash &torrentsFilesChangedProperties) const;
        void updateTorrentsInDb(
                const TorrentsChangedHash &torrentsChangedHash,
                const TorrentsFilesChangedHash &torrentsFilesChangedHash) const;
        void updateTorrentInDb(
                TorrentId torrentId,
                const QSharedPointer<const TorrentChangedProperties> changedProperties) const;
#if LOG_CHANGED_TORRENTS
        void updatePreviewableFilesInDb(
                const QSharedPointer<const TorrentFilesChangedHash> changedFilesProperties,
                TorrentId torrentId) const;
#else
        void updatePreviewableFilesInDb(
                const QSharedPointer<const TorrentFilesChangedHash> changedFilesProperties) const;
#endif
        /*! Find out changed properties in updated torrents. */
        void traceTorrentChangedProperties(
                const TorrentHandleByIdHash &torrentsUpdated,
                const TorrentSqlRecordByIdHash &torrentsInDb,
                TorrentsChangedHash &torrentsChangedProperties) const;
        /*! Find out changed properties in updated torrent files. */
        void traceTorrentFilesChangedProperties(
                const TorrentHandleByIdHash &torrentsUpdated,
                const TorrentFileSqlRecordByIdHash &torrentsFilesInDb,
                TorrentsFilesChangedHash &torrentsFilesChangedProperties) const;

        QScopedPointer<TorrentHandleByInfoHashHash> m_torrentsToCommit;
        QPointer<QTimer> m_dbCommitTimer;
        HWND m_qMediaHwnd = nullptr;
        bool m_qMediaWindowActive = false;

    private slots:
        void handleTorrentAdded(BitTorrent::TorrentHandle *const torrent) const;
        void handleTorrentDeleted(BitTorrent::InfoHash infoHash) const;
        void commitTorrentsTimerTimeout();
        void handleTorrentsUpdated(const QVector<BitTorrent::TorrentHandle *> &torrents);
        void handleTorrentStorageMoveFinished(const BitTorrent::TorrentHandle *const torrent,
                                              const QString &newPath) const;
    };

    // QHash requirements
    uint qHash(const TorrentStatus &torrentStatus, uint seed);
}

#endif // TORRENTEXPORTER_H
