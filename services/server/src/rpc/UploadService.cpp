#include "UploadService.h"
#include <filesystem>
#include <random>

UploadTransaction::UploadTransaction(StorageService &value,
                                     avserver::PlaybackEvents *sink,
                                     std::string sessionId,
                                     std::string fileName)
    : storage(value), events(sink), session(std::move(sessionId)),
      originalName(std::move(fileName)) {
  if (session.empty() ||
      session.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQR"
                                "STUVWXYZ0123456789-_") != std::string::npos)
    throw std::invalid_argument("invalid session");

  std::random_device random;
  const char *hex = "0123456789abcdef";
  for (int i = 0; i < 32; ++i)
    id += hex[random() % 16];

  auto root = std::getenv("AVPLAYER_STORAGE_ROOT");
  directory = std::filesystem::path(root ? root : "storage") / session / id;
  try {
    std::filesystem::create_directories(directory);
    auto suffix = std::filesystem::path(originalName).extension().string();
    if (suffix.empty())
      suffix = ".bin";
    file.open(directory / ("original" + suffix), std::ios::binary);
    if (!file)
      throw std::runtime_error("cannot create upload");
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    throw;
  }
}

UploadTransaction::~UploadTransaction() {
  if (!completed) {
    file.close();
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
  }
}

void UploadTransaction::Write(const std::string &bytes) {
  if (completed || !file.is_open())
    throw std::runtime_error("upload is not open");
  file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!file)
    throw std::runtime_error("upload write failed");
  size += static_cast<int64_t>(bytes.size());
}

avplayer::agent::v1::MediaHandle UploadTransaction::Finish() {
  if (completed || !file.is_open())
    throw std::runtime_error("upload is not open");
  file.close();
  if (!file)
    throw std::runtime_error("upload close failed");

  namespace api = avplayer::agent::v1;
  auto agentAddress = std::getenv("AVPLAYER_AGENT_SERVER");
  auto stub = api::AgentService::NewStub(
      grpc::CreateChannel(agentAddress ? agentAddress : "localhost:50061",
                          grpc::InsecureChannelCredentials()));
  grpc::ClientContext context;
  context.set_deadline(std::chrono::system_clock::now() +
                       std::chrono::seconds(5));
  api::MediaRegistration registration;
  registration.set_session_id(session);
  registration.set_media_id(id);
  api::ControlAck ack;
  auto status = stub->RegisterMedia(&context, registration, &ack);
  if (!status.ok() || !ack.success())
    throw std::runtime_error("Agent media registration failed");

  api::Struct request, response;
  request.set_json(nlohmann::json{{"action", "record_media"},
                                  {"session_id", session},
                                  {"media_id", id},
                                  {"file_name", originalName},
                                  {"size_bytes", size}}
                       .dump());
  storage.Execute(nullptr, &request, &response);

  api::MediaHandle handle;
  handle.set_media_id(id);
  handle.set_size_bytes(size);
  completed = true;
  if (events)
    events->Record("media.upload_completed", session, id, "",
                   {{"size_bytes", size}});
  return handle;
}

std::unique_ptr<UploadTransaction>
UploadService::BeginUpload(const std::string &session,
                           const std::string &fileName) {
  return std::unique_ptr<UploadTransaction>(
      new UploadTransaction(storage, events, session, fileName));
}

grpc::Status UploadService::UploadMedia(
    grpc::ServerContext *serverContext,
    grpc::ServerReader<avplayer::agent::v1::MediaChunk> *reader,
    avplayer::agent::v1::MediaHandle *handle) {
  namespace fs = std::filesystem;
  namespace api = avplayer::agent::v1;
  api::MediaChunk part;
  if (!reader->Read(&part) || part.session_id().empty())
    return {grpc::StatusCode::INVALID_ARGUMENT, "session_id required"};
  try {
    auto upload = BeginUpload(part.session_id(), part.file_name());
    do {
      upload->Write(part.data());
      if (serverContext->IsCancelled())
        return {grpc::StatusCode::CANCELLED, "upload cancelled"};
    } while (reader->Read(&part));
    if (serverContext->IsCancelled())
      return {grpc::StatusCode::CANCELLED, "upload cancelled"};
    *handle = upload->Finish();
    return grpc::Status::OK;
  } catch (const std::exception &e) {
    return {grpc::StatusCode::INTERNAL, e.what()};
  }
}
