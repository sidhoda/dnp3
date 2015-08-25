/**
 * Licensed to Green Energy Corp (www.greenenergycorp.com) under one or
 * more contributor license agreements. See the NOTICE file distributed
 * with this work for additional information regarding copyright ownership.
 * Green Energy Corp licenses this file to you under the Apache License,
 * Version 2.0 (the "License"); you may not use this file except in
 * compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This project was forked on 01/01/2013 by Automatak, LLC and modifications
 * may have been made to this file. Automatak, LLC licenses these modifications
 * to you under the terms of the License.
 */
#include <catch.hpp>

#include <opendnp3/ErrorCodes.h>
#include <opendnp3/link/LinkFrame.h>

#include <openpal/util/ToHex.h>
#include <openpal/container/Buffer.h>

#include "BufferSegment.h"
#include "LinkLayerTest.h"

#include <testlib/HexConversions.h>

#include <iostream>

using namespace openpal;
using namespace opendnp3;
using namespace testlib;

#define SUITE(name) "LinkLayerTestSuite - " name

// All operations should fail except for OnLowerLayerUp, just a representative
// number of them
TEST_CASE(SUITE("ClosedState"))
{
	LinkLayerTest t;
	BufferSegment segment(250, "00");
	t.upper.SendDown(segment);
	REQUIRE(t.log.PopOneEntry(flags::ERR));
	t.link.OnLowerLayerDown();
	REQUIRE(t.log.PopOneEntry(flags::ERR));
	t.link.OnFrame(LinkFunction::SEC_ACK, false, false, false, 1, 2);
	REQUIRE(t.log.PopOneEntry(flags::ERR));
}

// Prove that the upper layer is notified when the lower layer comes online
TEST_CASE(SUITE("ForwardsOnLowerLayerUp"))
{
	LinkLayerTest t;

	REQUIRE_FALSE(t.upper.IsOnline());
	t.link.OnLowerLayerUp();
	REQUIRE(t.upper.IsOnline());
	t.link.OnLowerLayerUp();
	REQUIRE(t.log.PopUntil(flags::ERR));
}

// Check that once the layer comes up, validation errors can occur
TEST_CASE(SUITE("ValidatesMasterOutstationBit"))
{
	LinkLayerTest t; t.link.OnLowerLayerUp();
	t.link.OnFrame(LinkFunction::SEC_ACK, true, false, false, 1, 1024);
	REQUIRE(t.log.PopErrorCode(DLERR_WRONG_MASTER_BIT));
}

// Only process frames from your designated remote address
TEST_CASE(SUITE("ValidatesSourceAddress"))
{
	LinkLayerTest t; t.link.OnLowerLayerUp();
	t.link.OnFrame(LinkFunction::SEC_ACK, false, false, false, 1, 1023);
	REQUIRE(t.log.PopErrorCode(DLERR_UNKNOWN_SOURCE));
}

// This should actually never happen when using the LinkLayerRouter
// Only process frame addressed to you
TEST_CASE(SUITE("ValidatesDestinationAddress"))
{
	LinkLayerTest t;  t.link.OnLowerLayerUp();
	t.link.OnFrame(LinkFunction::SEC_ACK, false, false, false, 2, 1024);
	REQUIRE(t.log.PopErrorCode(DLERR_UNKNOWN_DESTINATION));
}

// Show that the base state of idle logs SecToPri frames as errors
TEST_CASE(SUITE("SecToPriNoContext"))
{
	LinkLayerTest t; t.link.OnLowerLayerUp();

	REQUIRE(t.log.IsLogErrorFree());
	t.link.OnFrame(LinkFunction::SEC_ACK, false, false, false, 1, 1024);
	REQUIRE(t.log.NextErrorCode() ==  DLERR_UNEXPECTED_LPDU);
}

// Show that the base state of idle forwards unconfirmed user data
TEST_CASE(SUITE("UnconfirmedDataPassedUpFromIdleUnreset"))
{
	LinkLayerTest t; t.link.OnLowerLayerUp();
	ByteStr bs(250, 0);
	t.link.OnFrame(LinkFunction::PRI_UNCONFIRMED_USER_DATA, false, false, false, 1, 1024, bs.ToRSlice());
	REQUIRE(t.log.IsLogErrorFree());
	REQUIRE(t.upper.receivedQueue.front() == bs.ToHex());
}

// Show that the base state of idle forwards unconfirmed user data
TEST_CASE(SUITE("ConfirmedDataIgnoredFromIdleUnreset"))
{
	LinkLayerTest t; t.link.OnLowerLayerUp();
	ByteStr bs(250, 0);
	t.link.OnFrame(LinkFunction::PRI_CONFIRMED_USER_DATA, false, false, false, 1, 1024, bs.ToRSlice());
	REQUIRE(t.upper.receivedQueue.empty());
	REQUIRE(t.log.NextErrorCode() == DLERR_UNEXPECTED_LPDU);
}

// Secondary Reset Links
TEST_CASE(SUITE("SecondaryResetLink"))
{
	LinkLayerTest t(LinkLayerTest::DefaultConfig());
	t.link.OnLowerLayerUp();
	t.link.OnFrame(LinkFunction::PRI_RESET_LINK_STATES, false, false, false, 1, 1024);

	Buffer buffer(292);
	auto writeTo = buffer.GetWSlice();
	auto frame = LinkFrame::FormatAck(writeTo, true, false, 1024, 1, nullptr);

	REQUIRE(t.numWrites ==  1);
	REQUIRE(ToHex(t.lastWrite) ==  ToHex(frame));
}

TEST_CASE(SUITE("SecAckWrongFCB"))
{
	LinkConfig cfg = LinkLayerTest::DefaultConfig();
	cfg.UseConfirms = true;

	LinkLayerTest t(cfg);
	t.link.OnLowerLayerUp();

	t.link.OnFrame(LinkFunction::PRI_RESET_LINK_STATES, false, false, false, 1, 1024);
	REQUIRE(t.numWrites == 1);
	t.link.OnTransmitResult(true);

	ByteStr b(250, 0);
	t.link.OnFrame(LinkFunction::PRI_CONFIRMED_USER_DATA, false, false, false, 1, 1024, b.ToRSlice());
	t.link.OnTransmitResult(true);
	REQUIRE(t.numWrites ==  2);

	Buffer buffer(292);
	auto writeTo = buffer.GetWSlice();
	auto frame = LinkFrame::FormatAck(writeTo, true, false, 1024, 1, nullptr);

	REQUIRE(ToHex(t.lastWrite) ==  ToHex(frame));
	REQUIRE(t.upper.receivedQueue.empty()); //data should not be passed up!
	REQUIRE(t.log.PopUntil(flags::WARN));
}

// When we get another reset links when we're already reset,
// ACK it and reset the link state
TEST_CASE(SUITE("SecondaryResetResetLinkStates"))
{
	LinkLayerTest t;
	t.link.OnLowerLayerUp();
	t.link.OnFrame(LinkFunction::PRI_RESET_LINK_STATES, false, false, false, 1, 1024);
	REQUIRE(t.numWrites == 1);
	t.link.OnTransmitResult(true);

	t.link.OnFrame(LinkFunction::PRI_RESET_LINK_STATES, false, false, false, 1, 1024);
	REQUIRE(t.numWrites == 2);
	t.link.OnTransmitResult(true);

	Buffer buffer(292);
	auto writeTo = buffer.GetWSlice();
	auto frame = LinkFrame::FormatAck(writeTo, true, false, 1024, 1, nullptr);

	REQUIRE(t.numWrites ==  2);
	REQUIRE(ToHex(t.lastWrite) ==  ToHex(frame));
}

TEST_CASE(SUITE("SecondaryResetConfirmedUserData"))
{
	LinkLayerTest t;
	t.link.OnLowerLayerUp();
	t.link.OnFrame(LinkFunction::PRI_RESET_LINK_STATES, false, false, false, 1, 1024);
	REQUIRE(t.numWrites == 1);
	t.link.OnTransmitResult(true);

	ByteStr bytes(250, 0);
	t.link.OnFrame(LinkFunction::PRI_CONFIRMED_USER_DATA, false, true, false, 1, 1024, bytes.ToRSlice());
	REQUIRE(t.numWrites ==  2);
	t.link.OnTransmitResult(true);

	REQUIRE(t.upper.receivedQueue.front() == bytes.ToHex());
	REQUIRE(t.log.IsLogErrorFree());
	t.upper.receivedQueue.clear();

	t.link.OnFrame(LinkFunction::PRI_CONFIRMED_USER_DATA, false, true, false, 1, 1024, bytes.ToRSlice()); //send with wrong FCB
	REQUIRE(t.numWrites ==  3); //should still get an ACK
	REQUIRE(t.upper.receivedQueue.empty()); //but no data
	REQUIRE(t.log.PopUntil(flags::WARN));
}

TEST_CASE(SUITE("RequestStatusOfLink"))
{
	LinkLayerTest t;
	t.link.OnLowerLayerUp();
	t.link.OnFrame(LinkFunction::PRI_REQUEST_LINK_STATUS, false, false, false, 1, 1024); //should be able to request this before the link is reset
	REQUIRE(t.numWrites ==  1);
	t.link.OnTransmitResult(true);

	Buffer buffer(292);
	auto writeTo = buffer.GetWSlice();
	auto frame = LinkFrame::FormatLinkStatus(writeTo, true, false, 1024, 1, nullptr);

	REQUIRE(ToHex(t.lastWrite) ==  ToHex(frame));

	t.link.OnFrame(LinkFunction::PRI_RESET_LINK_STATES, false, false, false, 1, 1024);
	REQUIRE(t.numWrites == 2);
	t.link.OnTransmitResult(true);

	t.link.OnFrame(LinkFunction::PRI_REQUEST_LINK_STATUS, false, false, false, 1, 1024); //should be able to request this before the link is reset
	REQUIRE(t.numWrites ==  3);
	REQUIRE(ToHex(t.lastWrite) ==  ToHex(frame));
}

TEST_CASE(SUITE("TestLinkStates"))
{
	LinkLayerTest t;
	t.link.OnLowerLayerUp();
	t.link.OnFrame(LinkFunction::PRI_TEST_LINK_STATES, false, false, false, 1, 1024);
	REQUIRE(t.numWrites == 0);
	REQUIRE(t.log.PopOneEntry(flags::WARN));

	t.link.OnFrame(LinkFunction::PRI_RESET_LINK_STATES, false, false, false, 1, 1024);
	REQUIRE(t.numWrites ==  1);
	t.link.OnTransmitResult(true);

	t.link.OnFrame(LinkFunction::PRI_TEST_LINK_STATES, false, true, false, 1, 1024);
	Buffer buffer(292);
	auto writeTo = buffer.GetWSlice();
	auto frame = LinkFrame::FormatAck(writeTo, true, false, 1024, 1, nullptr);
	REQUIRE(t.numWrites ==  2);
	REQUIRE(ToHex(t.lastWrite) ==  ToHex(frame));
}

TEST_CASE(SUITE("SendUnconfirmed"))
{
	LinkLayerTest t;
	t.link.OnLowerLayerUp();

	ByteStr bytes(250, 0);
	BufferSegment segment(250, bytes.ToHex());
	t.link.Send(segment);
	REQUIRE(t.numWrites ==  1);
	t.link.OnTransmitResult(true);

	REQUIRE(t.lastWrite.Size() ==  292);
	
	REQUIRE(t.exe.RunMany() > 0);

	REQUIRE(t.upper.GetState().successCnt ==  1);
	REQUIRE(t.numWrites ==  1);
}


TEST_CASE(SUITE("CloseBehavior"))
{
	LinkLayerTest t;
	t.link.OnLowerLayerUp();

	ByteStr bytes(250, 0);
	BufferSegment segments(250, bytes.ToHex());
	t.link.Send(segments);
	t.link.OnTransmitResult(true);

	REQUIRE(t.exe.RunMany() > 0);

	REQUIRE(t.upper.CountersEqual(1, 0));
	t.link.OnLowerLayerDown(); //take it down during the middle of a send
	REQUIRE_FALSE(t.upper.IsOnline());


	t.link.OnLowerLayerUp();
	REQUIRE(t.upper.IsOnline());
	segments.Reset();
	t.link.Send(segments);
	REQUIRE(t.numWrites ==  2);

}

TEST_CASE(SUITE("ResetLinkTimerExpiration"))
{
	LinkConfig cfg = LinkLayerTest::DefaultConfig();
	cfg.UseConfirms = true;

	LinkLayerTest t(cfg);
	t.link.OnLowerLayerUp();

	ByteStr bytes(250, 0);
	BufferSegment segments(250, bytes.ToHex());
	t.link.Send(segments);
	REQUIRE(t.numWrites ==  1);
	t.link.OnTransmitResult(true); // reset link

	REQUIRE(t.exe.RunMany() > 0);

	Buffer buffer(292);
	auto writeTo = buffer.GetWSlice();
	auto result = LinkFrame::FormatResetLinkStates(writeTo, true, 1024, 1, nullptr);
	REQUIRE(ToHex(t.lastWrite) ==  ToHex(result));
	REQUIRE(t.upper.CountersEqual(0, 0));

	REQUIRE(t.log.IsLogErrorFree());
	t.exe.AdvanceTime(cfg.Timeout);
	REQUIRE(t.exe.RunMany() > 0);
	REQUIRE(t.upper.CountersEqual(0, 1));
	REQUIRE(t.log.PopOneEntry(flags::WARN));
}

TEST_CASE(SUITE("ResetLinkTimerExpirationWithRetry"))
{
	LinkConfig cfg = LinkLayerTest::DefaultConfig();
	cfg.NumRetry = 1;
	cfg.UseConfirms = true;

	LinkLayerTest t(cfg);
	t.link.OnLowerLayerUp();

	ByteStr bytes(250, 0);
	BufferSegment segments(250, bytes.ToHex());
	t.link.Send(segments);
	REQUIRE(t.numWrites == 1);
	t.link.OnTransmitResult(true);
	t.exe.AdvanceTime(cfg.Timeout);
	REQUIRE(t.exe.RunMany() > 0); // timeout the wait for Ack
	
	REQUIRE(t.upper.CountersEqual(0, 0)); //check that the send is still occuring
	REQUIRE(t.numWrites == 2);
	Buffer buffer(292);

	{
		auto writeTo = buffer.GetWSlice();
		auto result = LinkFrame::FormatResetLinkStates(writeTo, true, 1024, 1, nullptr);
		REQUIRE(ToHex(t.lastWrite) == ToHex(result)); // check that reset links got sent again
	}
	t.link.OnTransmitResult(true);
	
	t.link.OnFrame(LinkFunction::SEC_ACK, false, false, false, 1, 1024); // this time Ack it
	REQUIRE(t.numWrites == 3);
	{
		auto writeTo = buffer.GetWSlice();
		auto result = LinkFrame::FormatConfirmedUserData(writeTo, true, true, 1024, 1, bytes, bytes.Size(), nullptr);
		REQUIRE(ToHex(t.lastWrite) == ToHex(result)); // check that the data got sent
	}
	t.link.OnTransmitResult(true);

	t.exe.AdvanceTime(cfg.Timeout);
	REQUIRE(t.exe.RunMany() > 0); //timeout the ACK	
	REQUIRE(t.upper.CountersEqual(0, 1));

	// Test retry reset
	segments.Reset();
	t.link.Send(segments);
	REQUIRE(t.numWrites ==  4);
	t.link.OnTransmitResult(true);

	REQUIRE(t.log.IsLogErrorFree());
	t.exe.AdvanceTime(cfg.Timeout);
	REQUIRE(t.exe.RunMany() > 0);
	REQUIRE(t.upper.CountersEqual(0, 1)); //check that the send is still occuring
}

TEST_CASE(SUITE("ResetLinkTimerExpirationWithRetryResetState"))
{
	LinkConfig cfg = LinkLayerTest::DefaultConfig();
	cfg.NumRetry = 1;
	cfg.UseConfirms = true;

	LinkLayerTest t(cfg);
	t.link.OnLowerLayerUp();

	ByteStr bytes(250, 0);
	BufferSegment segments(250, bytes.ToHex());
	t.link.Send(segments);
	REQUIRE(t.numWrites == 1);
	t.link.OnTransmitResult(true);
	t.link.OnFrame(LinkFunction::SEC_ACK, false, false, false, 1, 1024);
	REQUIRE(t.numWrites == 2);
	t.link.OnTransmitResult(true);
	t.link.OnFrame(LinkFunction::SEC_ACK, false, false, false, 1, 1024);

	REQUIRE(t.exe.RunMany() > 0);
	REQUIRE(t.upper.CountersEqual(1, 0));

	segments.Reset();
	t.link.Send(segments);
	REQUIRE(t.numWrites ==  3);
	t.link.OnTransmitResult(true);

	REQUIRE(t.log.IsLogErrorFree());
	t.exe.AdvanceTime(cfg.Timeout);
	REQUIRE(t.exe.RunOne()); // timeout
	REQUIRE(t.upper.CountersEqual(1, 0)); //check that the send is still occuring
	REQUIRE(t.numWrites ==  4);
	t.link.OnTransmitResult(true);

	t.link.OnFrame(LinkFunction::SEC_ACK, false, false, false, 1, 1024);
	REQUIRE(t.exe.RunMany() > 0);
	REQUIRE(t.upper.CountersEqual(2, 0));

	// Test retry reset
	segments.Reset();
	t.link.Send(segments);
	REQUIRE(t.numWrites ==  5);	// Should now be waiting for an ACK with active timer
	t.link.OnTransmitResult(true);

	REQUIRE(t.log.IsLogErrorFree());
	t.exe.AdvanceTime(cfg.Timeout);
	REQUIRE(t.exe.RunOne());	
	REQUIRE(t.upper.CountersEqual(2, 0)); //check that the send is still occuring
}


TEST_CASE(SUITE("ConfirmedDataRetry"))
{
	LinkConfig cfg = LinkLayerTest::DefaultConfig();
	cfg.NumRetry = 1;
	cfg.UseConfirms = true;

	LinkLayerTest t(cfg); t.link.OnLowerLayerUp();

	ByteStr bytes(250, 0);
	BufferSegment segments(250, bytes.ToHex());
	t.link.Send(segments);
	t.link.OnTransmitResult(true);
	REQUIRE(t.numWrites ==  1); // Should now be waiting for an ACK with active timer

	t.link.OnFrame(LinkFunction::SEC_ACK, false, false, false, 1, 1024);
	REQUIRE(t.numWrites ==  2);
	t.link.OnTransmitResult(true);

	t.exe.AdvanceTime(cfg.Timeout);
	REQUIRE(t.exe.RunMany() > 0); //timeout the ConfData, check that it retransmits	
	REQUIRE(t.numWrites ==  3);

	Buffer buffer(292);
	auto writeTo = buffer.GetWSlice();
	auto frame = LinkFrame::FormatConfirmedUserData(writeTo, true, true, 1024, 1, bytes, bytes.Size(), nullptr);

	REQUIRE(ToHex(t.lastWrite) ==  ToHex(frame));
	t.link.OnTransmitResult(true);

	t.link.OnFrame(LinkFunction::SEC_ACK, false, false, false, 1, 1024);
	REQUIRE(t.exe.RunMany() > 0);
	REQUIRE(t.numWrites ==  3);
	REQUIRE(t.upper.CountersEqual(1, 0));
}

TEST_CASE(SUITE("ResetLinkRetries"))
{
	LinkConfig cfg = LinkLayerTest::DefaultConfig();
	cfg.NumRetry = 3;
	cfg.UseConfirms = true;

	LinkLayerTest t(cfg); t.link.OnLowerLayerUp();

	ByteStr bytes(250, 0);
	BufferSegment segments(250, bytes.ToHex());
	t.link.Send(segments);

	for(int i = 1; i < 5; ++i)
	{
		REQUIRE(t.numWrites ==  i); // sends link retry
		Buffer buffer(292);
		auto writeTo = buffer.GetWSlice();
		auto frame = LinkFrame::FormatResetLinkStates(writeTo, true, 1024, 1, nullptr);
		REQUIRE(ToHex(t.lastWrite) == ToHex(frame));
		t.link.OnTransmitResult(true);
		t.exe.AdvanceTime(cfg.Timeout);
		REQUIRE(t.exe.RunMany() > 0); //timeout
	}

	REQUIRE(t.numWrites ==  4);
}

TEST_CASE(SUITE("ConfirmedDataNackDFCClear"))
{
	LinkConfig cfg = LinkLayerTest::DefaultConfig();
	cfg.NumRetry = 1;
	cfg.UseConfirms = true;

	LinkLayerTest t(cfg); t.link.OnLowerLayerUp();

	ByteStr bytes(250, 0);
	BufferSegment segments(250, bytes.ToHex());
	t.link.Send(segments);
	t.link.OnTransmitResult(true);
	REQUIRE(t.numWrites ==  1); // Should now be waiting for an ACK with active timer

	t.link.OnFrame(LinkFunction::SEC_ACK, false, false, false, 1, 1024);
	t.link.OnTransmitResult(true);
	REQUIRE(t.numWrites ==  2);  // num transmitting confirmed data

	t.link.OnFrame(LinkFunction::SEC_NACK, false, false, false, 1, 1024);  // test that we try to reset the link again
	t.link.OnTransmitResult(true);
	REQUIRE(t.numWrites ==  3);

	t.link.OnFrame(LinkFunction::SEC_ACK, false, false, false, 1, 1024); // ACK the link reset
	REQUIRE(t.numWrites ==  4);
}

TEST_CASE(SUITE("SendDataTimerExpiration"))
{
	LinkConfig cfg = LinkLayerTest::DefaultConfig();
	cfg.UseConfirms = true;

	LinkLayerTest t(cfg);
	t.link.OnLowerLayerUp();

	ByteStr bytes(250, 0);
	BufferSegment segments(250, bytes.ToHex());
	t.link.Send(segments);
	REQUIRE(t.numWrites ==  1);
	t.link.OnTransmitResult(true);

	t.link.OnFrame(LinkFunction::SEC_ACK, false, false, false, 1, 1024); // ACK the reset links
	REQUIRE(t.numWrites ==  2);
	Buffer buffer(292);
	auto writeTo = buffer.GetWSlice();
	auto frame = LinkFrame::FormatConfirmedUserData(writeTo, true, true, 1024, 1, bytes, bytes.Size(), nullptr);
	REQUIRE(t.numWrites ==  2);
	REQUIRE(ToHex(t.lastWrite) ==  ToHex(frame)); // check that data was sent
	t.link.OnTransmitResult(true);

	t.exe.AdvanceTime(cfg.Timeout);
	REQUIRE(t.exe.RunMany() > 0); //trigger the timeout callback
	REQUIRE(t.upper.CountersEqual(0, 1));
}


TEST_CASE(SUITE("SendDataSuccess"))
{
	LinkConfig cfg = LinkLayerTest::DefaultConfig();
	cfg.UseConfirms = true;

	LinkLayerTest t(cfg);
	t.link.OnLowerLayerUp();

	ByteStr bytes(250, 0);
	BufferSegment segments(250, bytes.ToHex());
	t.link.Send(segments);
	t.link.OnTransmitResult(true);
	t.link.OnFrame(LinkFunction::SEC_ACK, false, false, false, 1, 1024);
	t.link.OnTransmitResult(true);
	t.link.OnFrame(LinkFunction::SEC_ACK, false, false, false, 1, 1024);
	REQUIRE(t.exe.RunMany() > 0);
	REQUIRE(t.upper.CountersEqual(1, 0));

	segments.Reset();
	t.link.Send(segments); // now we should be directly sending w/o having to reset, and the FCB should flip
	Buffer buffer(292);
	auto writeTo = buffer.GetWSlice();
	auto frame = LinkFrame::FormatConfirmedUserData(writeTo, true, false, 1024, 1, bytes, bytes.Size(), nullptr);
	REQUIRE(t.numWrites ==  3);
	REQUIRE(ToHex(t.lastWrite) ==  ToHex(frame));
}

