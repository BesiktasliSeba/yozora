#include "config.h"
#include "YozoraIDB.h"
#include "YozoraPrivateSession.h"
#include "ClientOrigin.h"
#include "DatabaseProvider.h"
#include "IDBConnectionToClient.h"
#include "IDBConnectionToClientDelegate.h"
#include "IDBConnectionToServer.h"
#include "IDBConnectionToServerDelegate.h"
#include "IDBCursorInfo.h"
#include "IDBDatabaseNameAndVersion.h"
#include "IDBError.h"
#include "IDBGetAllRecordsData.h"
#include "IDBGetRecordData.h"
#include "IDBIndexInfo.h"
#include "IDBIterateCursorData.h"
#include "IDBKeyRangeData.h"
#include "IDBObjectStoreInfo.h"
#include "IDBOpenRequestData.h"
#include "IDBRequestData.h"
#include "IDBResultData.h"
#include "IDBServer.h"
#include "IDBTransactionInfo.h"
#include "IDBValue.h"
#include "ProcessIdentifier.h"
#include "UniqueIDBDatabaseConnection.h"
#include <pal/SessionID.h>
#include <sys/stat.h>
#include <wtf/CrossThreadCopier.h>
#include <wtf/FastMalloc.h>
#include <wtf/MainThread.h>
#include <wtf/HashSet.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/ThreadSafeRefCounted.h>
#include <wtf/WorkQueue.h>

// site databases run on a thread of their own like the network process does and the page talks to them
// through two delegates: what the page asks is copied to that thread, what the server answers is copied back
// to the core thread

namespace {

using namespace WebCore;

String& storage_directory()
{
    static NeverDestroyed<String> directory;
    return directory.get();
}

class Hub;

// the server side: answers arrive on the database thread and go to the core thread
class ServerSide final : public IDBServer::IDBConnectionToClientDelegate {
public:
    ServerSide(Hub& hub, IDBConnectionIdentifier identifier);
    std::optional<IDBConnectionIdentifier> identifier() const final { return m_identifier; }

    void didDeleteDatabase(const IDBResultData&) final;
    void didOpenDatabase(const IDBResultData&) final;
    void didAbortTransaction(const IDBResourceIdentifier&, const IDBError&) final;
    void didCommitTransaction(const IDBResourceIdentifier&, const IDBError&) final;
    void didCreateObjectStore(const IDBResultData&) final;
    void didDeleteObjectStore(const IDBResultData&) final;
    void didRenameObjectStore(const IDBResultData&) final;
    void didClearObjectStore(const IDBResultData&) final;
    void didCreateIndex(const IDBResultData&) final;
    void didDeleteIndex(const IDBResultData&) final;
    void didRenameIndex(const IDBResultData&) final;
    void didPutOrAdd(const IDBResultData&) final;
    void didGetRecord(const IDBResultData&) final;
    void didGetAllRecords(const IDBResultData&) final;
    void didGetCount(const IDBResultData&) final;
    void didDeleteRecord(const IDBResultData&) final;
    void didOpenCursor(const IDBResultData&) final;
    void didIterateCursor(const IDBResultData&) final;
    void fireVersionChangeEvent(IDBServer::UniqueIDBDatabaseConnection&, const IDBResourceIdentifier&, uint64_t) final;
    void generateIndexKeyForRecord(const IDBResourceIdentifier&, const IDBIndexInfo&, const std::optional<IDBKeyPath>&, const IDBKeyData&, const IDBValue&, std::optional<int64_t>) final;
    void didStartTransaction(const IDBResourceIdentifier&, const IDBError&) final;
    void didCloseFromServer(IDBServer::UniqueIDBDatabaseConnection&, const IDBError&) final;
    void notifyOpenDBRequestBlocked(const IDBResourceIdentifier&, uint64_t, uint64_t) final;
    void didGetAllDatabaseNamesAndVersions(const IDBResourceIdentifier&, Vector<IDBDatabaseNameAndVersion>&&) final;

private:
    // one answer of the server, run on the core thread against the connection the page holds
    void toClient(Function<void(IDBClient::IDBConnectionToServer&)>&&);
    void result(void (IDBClient::IDBConnectionToServer::*function)(const IDBResultData&), const IDBResultData&);

    Hub& m_hub;
    IDBConnectionIdentifier m_identifier;
};

// the client side: what the page asks goes to the database thread
class ClientSide final : public IDBClient::IDBConnectionToServerDelegate {
public:
    explicit ClientSide(Hub& hub) : m_hub(hub) { }
    void ref() const final;
    void deref() const final;
    std::optional<IDBConnectionIdentifier> identifier() const final;

    void deleteDatabase(const IDBOpenRequestData&) final;
    void openDatabase(const IDBOpenRequestData&) final;
    void abortTransaction(const IDBResourceIdentifier&) final;
    void commitTransaction(const IDBResourceIdentifier&, uint64_t) final;
    void didFinishHandlingVersionChangeTransaction(IDBDatabaseConnectionIdentifier, const IDBResourceIdentifier&) final;
    void createObjectStore(const IDBRequestData&, const IDBObjectStoreInfo&) final;
    void deleteObjectStore(const IDBRequestData&, const String&) final;
    void renameObjectStore(const IDBRequestData&, IDBObjectStoreIdentifier, const String&) final;
    void clearObjectStore(const IDBRequestData&, IDBObjectStoreIdentifier) final;
    void createIndex(const IDBRequestData&, const IDBIndexInfo&) final;
    void deleteIndex(const IDBRequestData&, IDBObjectStoreIdentifier, const String&) final;
    void renameIndex(const IDBRequestData&, IDBObjectStoreIdentifier, IDBIndexIdentifier, const String&) final;
    void putOrAdd(const IDBRequestData&, const IDBKeyData&, const IDBValue&, const IndexIDToIndexKeyMap&, const IndexedDB::ObjectStoreOverwriteMode) final;
    void getRecord(const IDBRequestData&, const IDBGetRecordData&) final;
    void getAllRecords(const IDBRequestData&, const IDBGetAllRecordsData&) final;
    void getCount(const IDBRequestData&, const IDBKeyRangeData&) final;
    void deleteRecord(const IDBRequestData&, const IDBKeyRangeData&) final;
    void openCursor(const IDBRequestData&, const IDBCursorInfo&) final;
    void iterateCursor(const IDBRequestData&, const IDBIterateCursorData&) final;
    void establishTransaction(IDBDatabaseConnectionIdentifier, const IDBTransactionInfo&) final;
    void databaseConnectionPendingClose(IDBDatabaseConnectionIdentifier) final;
    void databaseConnectionClosed(IDBDatabaseConnectionIdentifier) final;
    void abortOpenAndUpgradeNeeded(IDBDatabaseConnectionIdentifier, const std::optional<IDBResourceIdentifier>&) final;
    void didFireVersionChangeEvent(IDBDatabaseConnectionIdentifier, const IDBResourceIdentifier&, const IndexedDB::ConnectionClosedOnBehalfOfServer) final;
    void didGenerateIndexKeyForRecord(const IDBResourceIdentifier&, const IDBResourceIdentifier&, const IDBIndexInfo&, const IDBKeyData&, const IndexKey&, std::optional<int64_t>) final;
    void openDBRequestCancelled(const IDBOpenRequestData&) final;
    void getAllDatabaseNamesAndVersions(const IDBResourceIdentifier&, const ClientOrigin&) final;

private:
    Hub& m_hub;
};

// the pieces of one connection between the page and the databases, one for the profile and one for the
// private session. the private one has an empty directory, so its databases live in memory and end with the
// process or with clear
class Hub final {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(Hub);
public:
    explicit Hub(bool isPrivate)
        : m_queue(WorkQueue::create(isPrivate ? "org.yozora.IndexedDB.private"_s : "org.yozora.IndexedDB"_s))
        , m_clientSide(*this)
    {
        m_client = IDBClient::IDBConnectionToServer::create(m_clientSide, PAL::SessionID::defaultSessionID());
        String directory = isPrivate ? String() : storage_directory();
        if (!directory.isEmpty())
            mkdir(directory.utf8().data(), 0700);
        auto identifier = m_client->identifier();
        m_queue->dispatch([this, directory = directory.isolatedCopy(), identifier] {
            m_server = makeUnique<IDBServer::IDBServer>(directory, [](const ClientOrigin&, uint64_t) { return true; });
            m_serverSide = makeUnique<ServerSide>(*this, identifier);
            m_connectionToClient = IDBServer::IDBConnectionToClient::create(*m_serverSide);
            m_server->registerConnection(*m_connectionToClient);
        });
    }

    IDBClient::IDBConnectionToServer& client() { return *m_client; }
    IDBConnectionIdentifier identifier() const { return m_client->identifier(); }

    // the database thread runs the work in the order it was asked
    void onDatabaseThread(Function<void(IDBServer::IDBServer&)>&& work)
    {
        m_queue->dispatch([this, work = WTF::move(work)]() mutable { work(*m_server); });
    }

    bool hasOpenDatabases() const
    {
        ASSERT(isMainThread());
        return !m_pendingOpens.isEmpty() || !m_openDatabases.isEmpty();
    }

    void opening(const IDBOpenRequestData& data) { m_pendingOpens.add(data.requestIdentifier()); }
    void cancelled(const IDBOpenRequestData& data) { m_pendingOpens.remove(data.requestIdentifier()); }
    void closed(IDBDatabaseConnectionIdentifier connection) { m_openDatabases.remove(connection); }
    void opened(const IDBResultData& data)
    {
        if (data.type() == IDBResultType::OpenDatabaseSuccess || data.type() == IDBResultType::OpenDatabaseUpgradeNeeded)
            m_openDatabases.add(data.databaseConnectionIdentifier());
        m_pendingOpens.remove(data.requestIdentifier());
    }

    void clear()
    {
        onDatabaseThread([](IDBServer::IDBServer& server) { server.closeAndDeleteDatabasesModifiedSince(-WallTime::infinity()); });
    }

private:
    // touched only on the core thread, before handing requests or replies to WebCore
    HashSet<IDBResourceIdentifier> m_pendingOpens;
    HashSet<IDBDatabaseConnectionIdentifier> m_openDatabases;
    Ref<WorkQueue> m_queue;
    ClientSide m_clientSide;
    RefPtr<IDBClient::IDBConnectionToServer> m_client;
    std::unique_ptr<IDBServer::IDBServer> m_server;
    std::unique_ptr<ServerSide> m_serverSide;
    RefPtr<IDBServer::IDBConnectionToClient> m_connectionToClient;
};

std::unique_ptr<Hub>& hubStorage(bool isPrivate)
{
    static NeverDestroyed<std::unique_ptr<Hub>> profile;
    static NeverDestroyed<std::unique_ptr<Hub>> session;
    return isPrivate ? session.get() : profile.get();
}

Hub& hub(bool isPrivate)
{
    auto& instance = hubStorage(isPrivate);
    if (!instance)
        instance = makeUnique<Hub>(isPrivate);
    return *instance;
}

ServerSide::ServerSide(Hub& hubRef, IDBConnectionIdentifier identifier)
    : m_hub(hubRef)
    , m_identifier(identifier)
{
}

void ServerSide::toClient(Function<void(IDBClient::IDBConnectionToServer&)>&& work)
{
    callOnMainThread([this, work = WTF::move(work)]() mutable { work(m_hub.client()); });
}

void ServerSide::result(void (IDBClient::IDBConnectionToServer::*function)(const IDBResultData&), const IDBResultData& data)
{
    toClient([function, data = data.isolatedCopy()](IDBClient::IDBConnectionToServer& client) { (client.*function)(data); });
}

void ServerSide::didDeleteDatabase(const IDBResultData& data) { result(&IDBClient::IDBConnectionToServer::didDeleteDatabase, data); }
void ServerSide::didOpenDatabase(const IDBResultData& data)
{
    toClient([this, data = data.isolatedCopy()](auto& client) {
        m_hub.opened(data);
        client.didOpenDatabase(data);
    });
}
void ServerSide::didCreateObjectStore(const IDBResultData& data) { result(&IDBClient::IDBConnectionToServer::didCreateObjectStore, data); }
void ServerSide::didDeleteObjectStore(const IDBResultData& data) { result(&IDBClient::IDBConnectionToServer::didDeleteObjectStore, data); }
void ServerSide::didRenameObjectStore(const IDBResultData& data) { result(&IDBClient::IDBConnectionToServer::didRenameObjectStore, data); }
void ServerSide::didClearObjectStore(const IDBResultData& data) { result(&IDBClient::IDBConnectionToServer::didClearObjectStore, data); }
void ServerSide::didCreateIndex(const IDBResultData& data) { result(&IDBClient::IDBConnectionToServer::didCreateIndex, data); }
void ServerSide::didDeleteIndex(const IDBResultData& data) { result(&IDBClient::IDBConnectionToServer::didDeleteIndex, data); }
void ServerSide::didRenameIndex(const IDBResultData& data) { result(&IDBClient::IDBConnectionToServer::didRenameIndex, data); }
void ServerSide::didPutOrAdd(const IDBResultData& data) { result(&IDBClient::IDBConnectionToServer::didPutOrAdd, data); }
void ServerSide::didGetRecord(const IDBResultData& data) { result(&IDBClient::IDBConnectionToServer::didGetRecord, data); }
void ServerSide::didGetAllRecords(const IDBResultData& data) { result(&IDBClient::IDBConnectionToServer::didGetAllRecords, data); }
void ServerSide::didGetCount(const IDBResultData& data) { result(&IDBClient::IDBConnectionToServer::didGetCount, data); }
void ServerSide::didDeleteRecord(const IDBResultData& data) { result(&IDBClient::IDBConnectionToServer::didDeleteRecord, data); }
void ServerSide::didOpenCursor(const IDBResultData& data) { result(&IDBClient::IDBConnectionToServer::didOpenCursor, data); }
void ServerSide::didIterateCursor(const IDBResultData& data) { result(&IDBClient::IDBConnectionToServer::didIterateCursor, data); }

void ServerSide::didAbortTransaction(const IDBResourceIdentifier& transaction, const IDBError& error)
{
    toClient([transaction = transaction.isolatedCopy(), error = error.isolatedCopy()](auto& client) { client.didAbortTransaction(transaction, error); });
}

void ServerSide::didCommitTransaction(const IDBResourceIdentifier& transaction, const IDBError& error)
{
    toClient([transaction = transaction.isolatedCopy(), error = error.isolatedCopy()](auto& client) { client.didCommitTransaction(transaction, error); });
}

void ServerSide::fireVersionChangeEvent(IDBServer::UniqueIDBDatabaseConnection& connection, const IDBResourceIdentifier& request, uint64_t version)
{
    toClient([connection = connection.identifier(), request = request.isolatedCopy(), version](auto& client) { client.fireVersionChangeEvent(connection, request, version); });
}

void ServerSide::generateIndexKeyForRecord(const IDBResourceIdentifier& request, const IDBIndexInfo& info, const std::optional<IDBKeyPath>& path, const IDBKeyData& key, const IDBValue& value, std::optional<int64_t> recordID)
{
    toClient([request = request.isolatedCopy(), info = info.isolatedCopy(), path = crossThreadCopy(path), key = key.isolatedCopy(), value = value.isolatedCopy(), recordID](auto& client) {
        client.generateIndexKeyForRecord(request, info, path, key, value, recordID);
    });
}

void ServerSide::didStartTransaction(const IDBResourceIdentifier& transaction, const IDBError& error)
{
    toClient([transaction = transaction.isolatedCopy(), error = error.isolatedCopy()](auto& client) { client.didStartTransaction(transaction, error); });
}

void ServerSide::didCloseFromServer(IDBServer::UniqueIDBDatabaseConnection& connection, const IDBError& error)
{
    toClient([this, connection = connection.identifier(), error = error.isolatedCopy()](auto& client) {
        m_hub.closed(connection);
        client.didCloseFromServer(connection, error);
    });
}

void ServerSide::notifyOpenDBRequestBlocked(const IDBResourceIdentifier& request, uint64_t oldVersion, uint64_t newVersion)
{
    toClient([request = request.isolatedCopy(), oldVersion, newVersion](auto& client) { client.notifyOpenDBRequestBlocked(request, oldVersion, newVersion); });
}

void ServerSide::didGetAllDatabaseNamesAndVersions(const IDBResourceIdentifier& request, Vector<IDBDatabaseNameAndVersion>&& names)
{
    toClient([request = request.isolatedCopy(), names = crossThreadCopy(WTF::move(names))](auto& client) mutable { client.didGetAllDatabaseNamesAndVersions(request, WTF::move(names)); });
}

// the page's side: nothing is shared with the thread, every argument is copied before it crosses

void ClientSide::ref() const { }
void ClientSide::deref() const { }

// the connection is named by the process like the network process client does, the identifier is asked for
// while the connection is built
std::optional<IDBConnectionIdentifier> ClientSide::identifier() const { return Process::identifier(); }

void ClientSide::deleteDatabase(const IDBOpenRequestData& data)
{
    m_hub.onDatabaseThread([data = data.isolatedCopy()](auto& server) { server.deleteDatabase(data); });
}

void ClientSide::openDatabase(const IDBOpenRequestData& data)
{
    m_hub.opening(data);
    m_hub.onDatabaseThread([data = data.isolatedCopy()](auto& server) { server.openDatabase(data); });
}

void ClientSide::abortTransaction(const IDBResourceIdentifier& transaction)
{
    m_hub.onDatabaseThread([transaction = transaction.isolatedCopy()](auto& server) { server.abortTransaction(transaction); });
}

void ClientSide::commitTransaction(const IDBResourceIdentifier& transaction, uint64_t handled)
{
    m_hub.onDatabaseThread([transaction = transaction.isolatedCopy(), handled](auto& server) { server.commitTransaction(transaction, handled); });
}

void ClientSide::didFinishHandlingVersionChangeTransaction(IDBDatabaseConnectionIdentifier connection, const IDBResourceIdentifier& transaction)
{
    m_hub.onDatabaseThread([connection, transaction = transaction.isolatedCopy()](auto& server) { server.didFinishHandlingVersionChangeTransaction(connection, transaction); });
}

void ClientSide::createObjectStore(const IDBRequestData& request, const IDBObjectStoreInfo& info)
{
    m_hub.onDatabaseThread([request = request.isolatedCopy(), info = info.isolatedCopy()](auto& server) { server.createObjectStore(request, info); });
}

void ClientSide::deleteObjectStore(const IDBRequestData& request, const String& name)
{
    m_hub.onDatabaseThread([request = request.isolatedCopy(), name = name.isolatedCopy()](auto& server) { server.deleteObjectStore(request, name); });
}

void ClientSide::renameObjectStore(const IDBRequestData& request, IDBObjectStoreIdentifier store, const String& name)
{
    m_hub.onDatabaseThread([request = request.isolatedCopy(), store, name = name.isolatedCopy()](auto& server) { server.renameObjectStore(request, store, name); });
}

void ClientSide::clearObjectStore(const IDBRequestData& request, IDBObjectStoreIdentifier store)
{
    m_hub.onDatabaseThread([request = request.isolatedCopy(), store](auto& server) { server.clearObjectStore(request, store); });
}

void ClientSide::createIndex(const IDBRequestData& request, const IDBIndexInfo& info)
{
    m_hub.onDatabaseThread([request = request.isolatedCopy(), info = info.isolatedCopy()](auto& server) { server.createIndex(request, info); });
}

void ClientSide::deleteIndex(const IDBRequestData& request, IDBObjectStoreIdentifier store, const String& name)
{
    m_hub.onDatabaseThread([request = request.isolatedCopy(), store, name = name.isolatedCopy()](auto& server) { server.deleteIndex(request, store, name); });
}

void ClientSide::renameIndex(const IDBRequestData& request, IDBObjectStoreIdentifier store, IDBIndexIdentifier index, const String& name)
{
    m_hub.onDatabaseThread([request = request.isolatedCopy(), store, index, name = name.isolatedCopy()](auto& server) { server.renameIndex(request, store, index, name); });
}

void ClientSide::putOrAdd(const IDBRequestData& request, const IDBKeyData& key, const IDBValue& value, const IndexIDToIndexKeyMap& indexKeys, const IndexedDB::ObjectStoreOverwriteMode mode)
{
    m_hub.onDatabaseThread([request = request.isolatedCopy(), key = key.isolatedCopy(), value = value.isolatedCopy(), indexKeys = crossThreadCopy(indexKeys), mode](auto& server) {
        server.putOrAdd(request, key, value, indexKeys, mode);
    });
}

void ClientSide::getRecord(const IDBRequestData& request, const IDBGetRecordData& data)
{
    m_hub.onDatabaseThread([request = request.isolatedCopy(), data = data.isolatedCopy()](auto& server) { server.getRecord(request, data); });
}

void ClientSide::getAllRecords(const IDBRequestData& request, const IDBGetAllRecordsData& data)
{
    m_hub.onDatabaseThread([request = request.isolatedCopy(), data = data.isolatedCopy()](auto& server) { server.getAllRecords(request, data); });
}

void ClientSide::getCount(const IDBRequestData& request, const IDBKeyRangeData& range)
{
    m_hub.onDatabaseThread([request = request.isolatedCopy(), range = range.isolatedCopy()](auto& server) { server.getCount(request, range); });
}

void ClientSide::deleteRecord(const IDBRequestData& request, const IDBKeyRangeData& range)
{
    m_hub.onDatabaseThread([request = request.isolatedCopy(), range = range.isolatedCopy()](auto& server) { server.deleteRecord(request, range); });
}

void ClientSide::openCursor(const IDBRequestData& request, const IDBCursorInfo& info)
{
    m_hub.onDatabaseThread([request = request.isolatedCopy(), info = info.isolatedCopy()](auto& server) { server.openCursor(request, info); });
}

void ClientSide::iterateCursor(const IDBRequestData& request, const IDBIterateCursorData& data)
{
    m_hub.onDatabaseThread([request = request.isolatedCopy(), data = data.isolatedCopy()](auto& server) { server.iterateCursor(request, data); });
}

void ClientSide::establishTransaction(IDBDatabaseConnectionIdentifier connection, const IDBTransactionInfo& info)
{
    m_hub.onDatabaseThread([connection, info = info.isolatedCopy()](auto& server) { server.establishTransaction(connection, info); });
}

void ClientSide::databaseConnectionPendingClose(IDBDatabaseConnectionIdentifier connection)
{
    m_hub.onDatabaseThread([connection](auto& server) { server.databaseConnectionPendingClose(connection); });
}

void ClientSide::databaseConnectionClosed(IDBDatabaseConnectionIdentifier connection)
{
    m_hub.closed(connection);
    m_hub.onDatabaseThread([connection](auto& server) { server.databaseConnectionClosed(connection); });
}

void ClientSide::abortOpenAndUpgradeNeeded(IDBDatabaseConnectionIdentifier connection, const std::optional<IDBResourceIdentifier>& transaction)
{
    m_hub.closed(connection);
    m_hub.onDatabaseThread([connection, transaction = crossThreadCopy(transaction)](auto& server) { server.abortOpenAndUpgradeNeeded(connection, transaction); });
}

void ClientSide::didFireVersionChangeEvent(IDBDatabaseConnectionIdentifier connection, const IDBResourceIdentifier& request, const IndexedDB::ConnectionClosedOnBehalfOfServer closed)
{
    if (closed == IndexedDB::ConnectionClosedOnBehalfOfServer::Yes)
        m_hub.closed(connection);
    m_hub.onDatabaseThread([connection, request = request.isolatedCopy(), closed](auto& server) { server.didFireVersionChangeEvent(connection, request, closed); });
}

void ClientSide::didGenerateIndexKeyForRecord(const IDBResourceIdentifier& transaction, const IDBResourceIdentifier& request, const IDBIndexInfo& info, const IDBKeyData& key, const IndexKey& indexKey, std::optional<int64_t> recordID)
{
    m_hub.onDatabaseThread([transaction = transaction.isolatedCopy(), request = request.isolatedCopy(), info = info.isolatedCopy(), key = key.isolatedCopy(), indexKey = indexKey.isolatedCopy(), recordID](auto& server) {
        server.didGenerateIndexKeyForRecord(transaction, request, info, key, indexKey, recordID);
    });
}

void ClientSide::openDBRequestCancelled(const IDBOpenRequestData& data)
{
    m_hub.cancelled(data);
    m_hub.onDatabaseThread([data = data.isolatedCopy()](auto& server) { server.openDBRequestCancelled(data); });
}

void ClientSide::getAllDatabaseNamesAndVersions(const IDBResourceIdentifier& request, const ClientOrigin& origin)
{
    auto identifier = m_hub.identifier();
    m_hub.onDatabaseThread([identifier, request = request.isolatedCopy(), origin = origin.isolatedCopy()](auto& server) { server.getAllDatabaseNamesAndVersions(identifier, request, origin); });
}

class Provider final : public DatabaseProvider {
public:
    static Ref<Provider> create(bool isPrivate) { return adoptRef(*new Provider(isPrivate)); }
    IDBClient::IDBConnectionToServer& idbConnectionToServerForSession(PAL::SessionID) final { return hub(m_private).client(); }

private:
    explicit Provider(bool isPrivate) : m_private(isPrivate) { }
    bool m_private;
};

}

namespace YozoraIDB {

void setDirectory(const String& directory)
{
    storage_directory() = directory.isolatedCopy();
}

Ref<WebCore::DatabaseProvider> provider()
{
    // the page keeps the provider it was made with, so the session that was on when the page started is the one that holds its databases
    static NeverDestroyed<RefPtr<Provider>> profile;
    static NeverDestroyed<RefPtr<Provider>> session;
    bool isPrivate = YozoraPrivateSession::active();
    auto& instance = isPrivate ? session.get() : profile.get();
    if (!instance)
        instance = Provider::create(isPrivate);
    return *instance;
}

bool hasOpenDatabases()
{
    ASSERT(isMainThread());
    for (bool isPrivate : { false, true }) {
        auto& instance = hubStorage(isPrivate);
        if (instance && instance->hasOpenDatabases())
            return true;
    }
    return false;
}

void clear()
{
    hub(false).clear();
}

void clearPrivate()
{
    auto& instance = hubStorage(true);
    if (instance)
        instance->clear();
}

}
