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

#include "communicator.h"

Communicator::Communicator(local::stream_protocol::socket& socket,
    IProcessor& bg_processor, IProcessor& processor)
  : state_(State::OPEN), request_id_(0), socket_(socket),
  bg_processor_(bg_processor), processor_(processor),
  sending_messages_(false) {

    // Create a dummy header and calculate its size.
    // XXX: is there a way to compute the size of a Header
    // without creating a dummy object?
    NinjaMessage::Header dummy_header;
    dummy_header.set_size(0);
    dummy_header.set_type_id(0);
    dummy_header.set_request_id(0);
    header_size_ = dummy_header.ByteSize();

    // Start listening for messages on the background thread.
    bg_processor_.Post(boost::bind(&Communicator::AsyncReceiveMessage, this));
  }

Communicator::~Communicator() {
  assert(state_ == State::CLOSED);
}

void Communicator::EnqueueMessage(int request_id, int type_id,
    boost::shared_ptr<boost::asio::streambuf>& buf_message,
    const ErrorHandler_t& completion_handler) {

  // Enqueuing a message after this object closed is a programming error.
  assert(state_ != State::CLOSED);

  pending_messages_.push(PendingMessage{request_id, type_id, buf_message,
      completion_handler});

  // Restart the sending loop if needed.
  if (!sending_messages_) {
    SendNextMessage();
  }
}

void Communicator::SendNextMessage() {
  if (pending_messages_.empty()) {
    // There are no more messages to be sent.
    sending_messages_ = false;
    if (state_ == State::CLOSING) {
      // Now that all messages have been sent, we can close.
      processor_.Post(on_close_completed_);
      state_ = State::CLOSED;
    }
    return;
  }

  sending_messages_ = true;

  PendingMessage& msg = pending_messages_.front();
  AsyncSendMessage(msg.request_id_, msg.type_id_, msg.buf_message_,
      msg.completion_handler_);
  pending_messages_.pop();
}

void Communicator::OnConnectionClosed() {
  if (state_ == State::CLOSED)
    return;

  if (state_ == State::CLOSING) {
    // Notify that the async close operation completed.
    processor_.Post(on_close_completed_);
  }
  else if (on_connection_closed_) {
    // Notify that there was a network error.
    processor_.Post(on_connection_closed_);
  }

  state_ = State::CLOSED;
}

void Communicator::AsyncSendMessage(
    int request_id, int type_id,
    boost::shared_ptr<boost::asio::streambuf>& buf_message,
    const ErrorHandler_t& completion_handler) {
  boost::shared_ptr<NinjaMessage::Header> header(new NinjaMessage::Header());
  header->set_type_id(type_id);
  header->set_size(buf_message->size());
  header->set_request_id(request_id);

  boost::shared_ptr<boost::asio::streambuf> buf_header(
      new boost::asio::streambuf());
  std::ostream os(buf_header.get());

  if (!header->SerializeToOstream(&os)) {
    printf("Error while serializing header\n");
    completion_handler(RequestResult::SERIALIZATION_ERROR);
    return;
  }

  AsyncWriteHeader(buf_header, buf_message, completion_handler);
}

void Communicator::AsyncWriteHeader(
    boost::shared_ptr<boost::asio::streambuf>& buf_header,
    boost::shared_ptr<boost::asio::streambuf>& buf_message,
    const ErrorHandler_t& completion_handler) {

  async_write(socket_, *buf_header.get(),
      boost::bind(&Communicator::OnWriteHeader,
        this, boost::asio::placeholders::error,
        boost::asio::placeholders::bytes_transferred,
        buf_header, buf_message, completion_handler));
}

void Communicator::OnWriteHeader(
    boost::system::error_code err, size_t bytes_transferred,
    boost::shared_ptr<boost::asio::streambuf> buf_header,
    boost::shared_ptr<boost::asio::streambuf> buf_message,
    const ErrorHandler_t& completion_handler) {
  if (err) {
    printf("Error while sending header: %s\n", err.message().c_str());
    completion_handler(RequestResult::NETWORK_ERROR);
    OnConnectionClosed();
    return;
  }

  AsyncWriteMessage(buf_message, completion_handler);
}

void Communicator::AsyncWriteMessage(
    boost::shared_ptr<boost::asio::streambuf>& buf_message,
    const ErrorHandler_t& completion_handler) {

  async_write(socket_, *buf_message.get(),
      boost::bind(&Communicator::OnWriteMessage,
        this, boost::asio::placeholders::error,
        boost::asio::placeholders::bytes_transferred,
        buf_message, completion_handler));
}

void Communicator::OnWriteMessage(
    boost::system::error_code err, size_t bytes_transferred,
    boost::shared_ptr<boost::asio::streambuf> buf_message,
    const ErrorHandler_t& completion_handler) {
  if (err) {
    printf("Error while sending message\n");
    completion_handler(RequestResult::NETWORK_ERROR);
    OnConnectionClosed();
    return;
  }

  SendNextMessage();
}

void Communicator::AsyncReceiveMessage() {
  boost::shared_ptr<std::vector<char>> buf_header(
      new std::vector<char>(header_size_));

  async_read(socket_, boost::asio::buffer(*buf_header.get(), header_size_),
      boost::bind(&Communicator::OnReadHeader, this,
        boost::asio::placeholders::error,
        boost::asio::placeholders::bytes_transferred, buf_header));
}

void Communicator::OnReadHeader(const boost::system::error_code& err,
    size_t bytes_transferred,
    boost::shared_ptr<std::vector<char>> buf_header) {
  if (err) {
    OnConnectionClosed();
    return;
  }

  boost::shared_ptr<NinjaMessage::Header> header(new NinjaMessage::Header());
  std::string buf(buf_header->begin(), buf_header->end());
  if (!ParseMessage(*header.get(), buf)) {
    printf("Error while parsing header\n");
    // TODO: how to handle this error?
    return;
  }

  boost::shared_ptr<std::vector<char>> buf_message(
      new std::vector<char>(header->size()));

  async_read(socket_, boost::asio::buffer(*buf_message.get(), header->size()),
      boost::bind(&Communicator::OnReadMessage, this,
        boost::asio::placeholders::error,
        boost::asio::placeholders::bytes_transferred, header, buf_message));
}

void Communicator::OnReadMessage(const boost::system::error_code& err,
    size_t bytes_transferred,
    boost::shared_ptr<NinjaMessage::Header> header,
    boost::shared_ptr<std::vector<char>> buf_message) {
  if (err) {
    printf("Error while reading message\n");
    OnConnectionClosed();
    return;
  }

  std::string buf(buf_message->begin(), buf_message->end());
  OnMessageReceived(*header.get(), buf);

  if (state_ == State::OPEN) {
    // Start listening for new messages.
    AsyncReceiveMessage();
  }
  else {
    processor_.Post(on_close_completed_);
    state_ = State::CLOSED;
  }
}

void Communicator::AsyncClose(const OnCloseCompletedFn& onCloseCompleted) {
  if (state_ == State::CLOSED) {
    // The connection had already closed by itself.
    processor_.Post(onCloseCompleted);
    return;
  }

  // This is a programmer error, the user should call AsyncClose only once.
  assert(state_ != State::CLOSING);

  // This will cause the reading and writting loops to stop.
  // All messages that were enqueued will be sent before the completion handler
  // is called.
  state_ = State::CLOSING;
  on_close_completed_ = onCloseCompleted;
}
