// Copyright 2011 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef NINJA_COMMUNICATOR_H_
#define NINJA_COMMUNICATOR_H_

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <map>
#include <istream>
#include <ostream>
#include <queue>

#include "messages.pb.h"
#include "iprocessor.h"

using namespace std;
using namespace boost::asio;

enum RequestResult {
  SUCCESS,
  SERIALIZATION_ERROR,
  DESERIALIZATION_ERROR,
  NETWORK_ERROR
};

/// TODO: we need to enqueue the messages to be sent in order to be sure
/// that all the messages are in the correct order.
/// TODO: the completion handlers that are passed back to the client must be
/// posted on the main thread.
struct Communicator {
  private:
    /// Handler used to terminate the sending of a request.
    typedef boost::function<void(const RequestResult&)> ErrorHandler_t;
    /// Handler used to process a request.
    typedef boost::function<void(const NinjaMessage::Header&,
        const std::string& buf)> MessageHandler_t;

    struct PendingMessage {
      int request_id_;
      int type_id_;
      boost::shared_ptr<boost::asio::streambuf> buf_message_;
      const ErrorHandler_t completion_handler_;
    };

  public:
    Communicator(local::stream_protocol::socket& socket, IProcessor& bg_processor,
        IProcessor& processor);

    /// Set up a handler for a request.
    /// The server can use this in order to set up a handler to be called
    /// when a request of a certain type is received.
    template <class TMessage>
      void SetRequestHandler(const boost::function<void(int, const TMessage&)>& handler) {
        bg_processor_.Post(
            boost::bind(&Communicator::SetRequestHandlerBgThread<TMessage>, this,
              handler));
      }

    /// Send a request with a handler to be used for the response.
    /// The client can use this to send a request of a certain type
    /// and expect a response of a certain type. The given handler will
    /// be called with the response message when it is received.
    /// The client must check the RequestResult before going any further.
    /// RequestResult may have the following values:
    /// - SUCCESS: the server received the request and successfully replied;
    /// - SERIALIZATION_ERROR: the client could not serialize the message.
    ///   note that nothing was sent on the network when this error occurs;
    /// - DESERIALIZATION_ERROR: the server could not read the request;
    /// - NETWORK_ERROR: a network error prevented the message from being sent.
    template <class TRequest, class TResponse>
      void SendRequest(TRequest& request,
          const boost::function<void(const RequestResult&,
            const TResponse&)>& onCompleted) {
        bg_processor_.Post(
            boost::bind(&Communicator::SendRequestBgThread<TRequest, TResponse>, this,
              request, onCompleted));
      }

    /// Send a response to a request.
    /// The server can use this to send a response to a request it received.
    template <class TResponse>
      void SendReply(int request_id, TResponse& response) {
        bg_processor_.Post(
            boost::bind(&Communicator::SendReplyBgThread<TResponse>, this,
              request_id, response));
      }

  private:
    int request_id_;
    local::stream_protocol::socket& socket_;
    IProcessor& bg_processor_;
    IProcessor& processor_;
    /// Messages that are waiting to be sent.
    queue<PendingMessage> pending_messages_;
    bool sending_messages_;
    int header_size_;

    /// Map a message type_id to a handler.
    /// This keeps track of all types of request messages the user can respond
    /// to. When a message with a matching type_id is received, the correct
    /// handler will be called to inform the user of the request.
    typedef map<int, MessageHandler_t> RequestHandlers_t;
    RequestHandlers_t request_handlers_;

    /// Map a message request_id to a handler.
    /// This keeps track of all requests sent by the user for which it is
    /// waiting for a response.
    /// When a message with a matching request_id is received, the correct
    /// handler will be called to finish the transaction.
    typedef map<int, MessageHandler_t> ResponseHandlers_t;
    ResponseHandlers_t response_handlers_;

    template <class TMessage>
      bool SetRequestHandlerBgThread(const boost::function<void(int, const TMessage&)>& handler) {
        int type_id = TMessage::default_instance().type_id();

        MessageHandler_t message_handler =
          boost::bind(&Communicator::ParseRequest<TMessage>, this, _1, _2, handler);

        // If a handler already exists for this type, it is overwritten.
        request_handlers_[type_id] = message_handler;

        return true;
      }

    template <class TRequest, class TResponse>
      void SendRequestBgThread(TRequest& request,
          const boost::function<void(const RequestResult&,
            const TResponse&)>& onCompleted) {
        ++request_id_;

        request.set_type_id(TRequest::default_instance().type_id());

        ErrorHandler_t completion_handler = boost::bind(
            &Communicator::OnRequestFailed<TResponse>, this,
            _1, onCompleted, request_id_);

        // Set up the handler for the response. This hanler will be called
        // if a message is sent with the same request_id.
        boost::function<void(const RequestResult&, const TResponse&)> responseHandler =
          boost::bind(&Communicator::OnResponseReceived<TResponse>, this,
              _1, _2, onCompleted, request_id_);
        SetResponseHandler<TResponse>(request_id_, responseHandler);

        boost::shared_ptr<boost::asio::streambuf> buf(new boost::asio::streambuf());
        std::ostream os(buf.get());
        if (!request.SerializeToOstream(&os)) {
          printf("Error while serializing request message\n");
          completion_handler(RequestResult::SERIALIZATION_ERROR);
          return;
        }

        EnqueueMessage(request_id_, TRequest::default_instance().type_id(),
            buf, completion_handler);
      }

    template <class TResponse>
      void SendReplyBgThread(int request_id, TResponse& response) {
        response.set_type_id(TResponse::default_instance().type_id());

        ErrorHandler_t completion_handler = [](const RequestResult& res) {
          // TODO: Ignoring this error for now.
          (void)res;
        };

        boost::shared_ptr<boost::asio::streambuf> buf(new boost::asio::streambuf());
        std::ostream os(buf.get());
        if (!response.SerializeToOstream(&os)) {
          printf("Error while serializing response message\n");
          completion_handler(RequestResult::SERIALIZATION_ERROR);
          return;
        }

        EnqueueMessage(request_id, TResponse::default_instance().type_id(),
            buf, completion_handler);
      }

    /// Called when a request fails. A request might fail because of an
    /// error on the client (serialization, network, ...) or an error on the
    /// server (message not implemented).
    template <class TResponse>
      void OnRequestFailed(const RequestResult& res,
          const boost::function<void(const RequestResult&,
            const TResponse&)>& onCompleted, int request_id)
      {
        // Send dummy response.
        TResponse response;
        onCompleted(res, response);
        response_handlers_.erase(request_id);
      }

    /// Parse a message from a string.
    /// Return false if it fails.
    template <class TMessage>
      bool ParseMessage(TMessage& msg, const std::string& str) {
        return msg.ParseFromString(str);
      }

    /// Parse a request from the client.
    /// If this fails, the server is not informed of the request,
    /// The communicator takes care of notifying the client.
    template <class TRequest>
      void ParseRequest(const NinjaMessage::Header& header,
          const std::string& buf,
          const boost::function<void(int, const TRequest&)>& callback)
      {
        TRequest message;
        if (ParseMessage(message, buf)) {
          // Post the callback on the main thread.
          processor_.Post(boost::bind(callback, header.request_id(), message));
        }
        else {
          printf("Error while parsing request message\n");
          // TODO: handle the error: send back a DESERIALIZATION_ERROR.
          // The server cannot deserialize the client's message.
        }
      }

    /// Parse a server's response.
    template <class TResponse>
      void ParseResponse(const NinjaMessage::Header& header,
          const std::string& buf,
          const boost::function<void(const RequestResult& res,
            const TResponse&)>& callback)
      {
        TResponse message;
        if (ParseMessage(message, buf)) {
          callback(RequestResult::SUCCESS, message);
        }
        else {
          printf("Error while parsing response message\n");
          // The client cannot parse the server's response.
          callback(RequestResult::DESERIALIZATION_ERROR, message);
        }
      }

    /// Handler for a response.
    template <class TResponse>
      void OnResponseReceived(const RequestResult& req, const TResponse& response,
          const boost::function<void(const RequestResult&,
            const TResponse&)> onCompleted, int request_id) {

        processor_.Post(boost::bind(onCompleted, req, response));
        response_handlers_.erase(request_id);
      }

    /// Set up a handler for a response.
    template <class TResponse>
      void SetResponseHandler(int request_id,
          const boost::function<void(const RequestResult& res, const TResponse&)>& handler) {
#if 0
        ResponseHandlers_t::iterator itFind = response_handlers_.find(request_id);

        assert(itFind != response_handlers_.end());
#endif

        MessageHandler_t message_handler =
          boost::bind(&Communicator::ParseResponse<TResponse>, this, _1, _2, handler);

        response_handlers_[request_id] = message_handler;
      }

    /// Called when a new message is received.
    /// The first step is to determine if this message is a request or a
    /// response and the appropriate handler is called.
    void OnMessageReceived(const NinjaMessage::Header& header,
        const std::string& buf) {
      {
        // Try to find a request handler for this message.
        RequestHandlers_t::iterator itFind = request_handlers_.find(header.type_id());
        if (itFind != request_handlers_.end()) {
          itFind->second(header, buf);
          return;
        }
      }

      {
        // Try to find a response handler for this message.
        ResponseHandlers_t::iterator itFind = response_handlers_.find(header.request_id());
        if (itFind != response_handlers_.end()) {
          itFind->second(header, buf);
          return;
        }
      }

      // TODO: respond with error message
    }

    void EnqueueMessage(int request_id, int type_id,
        boost::shared_ptr<boost::asio::streambuf>& buf_message,
        const ErrorHandler_t& completion_handler);

    void SendNextMessage();

    void AsyncSendMessage(int request_id, int type_id,
        boost::shared_ptr<boost::asio::streambuf>& buf_message,
        const ErrorHandler_t& completion_handler);

    void AsyncWriteHeader(
        boost::shared_ptr<boost::asio::streambuf>& buf_header,
        boost::shared_ptr<boost::asio::streambuf>& buf_message,
        const ErrorHandler_t& completion_handler);

    void OnWriteHeader(
        boost::system::error_code err, size_t bytes_transferred,
        boost::shared_ptr<boost::asio::streambuf> buf_header,
        boost::shared_ptr<boost::asio::streambuf> buf_message,
        const ErrorHandler_t& completion_handler);

    void AsyncWriteMessage(
        boost::shared_ptr<boost::asio::streambuf>& buf_message,
        const ErrorHandler_t& completion_handler);

    void OnWriteMessage(
        boost::system::error_code err, size_t bytes_transferred,
        boost::shared_ptr<boost::asio::streambuf> buf_message,
        const ErrorHandler_t& completion_handler);

    void AsyncReceiveMessage();

    void OnReadHeader(const boost::system::error_code& err,
        size_t bytes_transferred,
        boost::shared_ptr<std::vector<char>> buf_header);

    void OnReadMessage(const boost::system::error_code& err,
        size_t bytes_transferred,
        boost::shared_ptr<NinjaMessage::Header> header,
        boost::shared_ptr<std::vector<char>> buf_message);
};

#endif // NINJA_COMMUNICATOR_H_

