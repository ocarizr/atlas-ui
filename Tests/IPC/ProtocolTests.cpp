#include <atlas/ipc/GrpcClient.hpp>
#include <gtest/gtest.h>

#include <session.grpc.pb.h>
#include <string>

TEST(ProtocolTests, RoundTripsTheVersionedSessionEnvelope)
{
    atlas::preview::SessionInfo source;
    source.set_protocol_version(1);
    source.set_session_id("test-session");
    atlas::preview::SessionInfo destination;
    ASSERT_TRUE(destination.ParseFromString(source.SerializeAsString()));
    EXPECT_EQ(destination.protocol_version(), 1U);
    EXPECT_EQ(destination.session_id(), "test-session");
}

TEST(ProtocolTests, RejectsMalformedWireData)
{
    atlas::preview::SessionInfo destination;
    EXPECT_FALSE(destination.ParseFromString(std::string(1, static_cast<char>(0xFF))));
}

TEST(ProtocolTests, CreatesTheGeneratedGrpcClientWithoutSendingRequests)
{
    const auto channel = grpc::CreateChannel("localhost:1", grpc::InsecureChannelCredentials());
    const auto client = atlas::preview::PreviewControl::NewStub(channel);
    EXPECT_NE(client, nullptr);
}
