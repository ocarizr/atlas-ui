#include <atlas/ipc/grpc_client.hpp>
#include <gtest/gtest.h>

#include <session.grpc.pb.h>
#include <string>

TEST(protocol_tests, round_trips_the_versioned_session_envelope)
{
    atlas::preview::SessionInfo source;
    source.set_protocol_version(1);
    source.set_session_id("test-session");
    atlas::preview::SessionInfo destination;
    ASSERT_TRUE(destination.ParseFromString(source.SerializeAsString()));
    EXPECT_EQ(destination.protocol_version(), 1U);
    EXPECT_EQ(destination.session_id(), "test-session");
}

TEST(protocol_tests, rejects_malformed_wire_data)
{
    atlas::preview::SessionInfo destination;
    EXPECT_FALSE(destination.ParseFromString(std::string(1, static_cast<char>(0xFF))));
}

TEST(protocol_tests, creates_the_generated_grpc_client_without_sending_requests)
{
    const auto channel = grpc::CreateChannel("localhost:1", grpc::InsecureChannelCredentials());
    const auto client = atlas::preview::PreviewControl::NewStub(channel);
    EXPECT_NE(client, nullptr);
}
