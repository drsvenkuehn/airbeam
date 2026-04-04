// T019 — ReceiverList unit tests
#include <gtest/gtest.h>
#include "discovery/ReceiverList.h"
#include "discovery/AirPlayReceiver.h"
#include <windows.h>

// ── Helpers ───────────────────────────────────────────────────────────────────

static AirPlayReceiver MakeReceiver(const std::wstring& name, bool airplay1 = true,
                                     ULONGLONG tick = 0)
{
    AirPlayReceiver r{};
    r.instanceName       = name;
    r.displayName        = name;
    r.isAirPlay1Compatible = airplay1;
    r.lastSeenTick       = tick;
    return r;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST(ReceiverList, Update_AirPlay1_Accepted)
{
    ReceiverList list(nullptr);
    auto r = MakeReceiver(L"Device1", true, 1000);
    list.Update(r);
    auto snap = list.Snapshot();
    ASSERT_EQ(snap.size(), 1u);
    EXPECT_EQ(snap[0].instanceName, L"Device1");
}

TEST(ReceiverList, Update_AirPlay2_Rejected)
{
    ReceiverList list(nullptr);
    auto r = MakeReceiver(L"Device2", false, 1000);
    list.Update(r);
    auto snap = list.Snapshot();
    EXPECT_TRUE(snap.empty());
}

TEST(ReceiverList, PruneStale_RemovesOldEntries)
{
    ReceiverList list(nullptr);
    auto r = MakeReceiver(L"OldDevice", true, 0ULL);
    list.Update(r);
    ASSERT_EQ(list.Snapshot().size(), 1u);
    // Prune with time 61 seconds later
    list.PruneStale(61000ULL);
    EXPECT_TRUE(list.Snapshot().empty());
}

TEST(ReceiverList, PruneStale_KeepsRecentEntries)
{
    ReceiverList list(nullptr);
    auto r = MakeReceiver(L"NewDevice", true, 50000ULL);
    list.Update(r);
    ASSERT_EQ(list.Snapshot().size(), 1u);
    // Prune with time 60 seconds after device was first seen
    list.PruneStale(110000ULL);
    EXPECT_EQ(list.Snapshot().size(), 1u);
}

TEST(ReceiverList, Remove_RemovesEntry)
{
    ReceiverList list(nullptr);
    auto r = MakeReceiver(L"ToRemove", true, 1000ULL);
    list.Update(r);
    ASSERT_EQ(list.Snapshot().size(), 1u);
    list.Remove(L"ToRemove");
    EXPECT_TRUE(list.Snapshot().empty());
}

TEST(ReceiverList, Snapshot_ReturnsFilteredCopy)
{
    ReceiverList list(nullptr);
    list.Update(MakeReceiver(L"A1", true, 1000ULL));
    list.Update(MakeReceiver(L"A2", false, 1000ULL)); // AirPlay 2, filtered out
    list.Update(MakeReceiver(L"A3", true, 1000ULL));
    auto snap = list.Snapshot();
    ASSERT_EQ(snap.size(), 2u);
    EXPECT_EQ(snap[0].instanceName, L"A1");
    EXPECT_EQ(snap[1].instanceName, L"A3");
}
