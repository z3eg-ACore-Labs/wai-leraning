#include "../../src/action/ActionLease.h"
#include "../../src/action/ClientIdentity.h"
#include "../../src/action/ControlModeCommand.h"
#include <cassert>
int main()
{
    Wai::Lease lease;
    assert(!lease.Fresh(0));
    lease.active = true; lease.generation = "fixture"; lease.heartbeat = lease.observed = 100; lease.sequence = 1;
    assert(!lease.Consume("other", 1, 101));
    assert(!lease.Consume("fixture", 2, 101));
    assert(lease.Consume("fixture", 1, 101));
    assert(!lease.Consume("fixture", 1, 102));
    lease.sequence = 2;
    assert(!lease.Consume("fixture", 2, 1101));
    assert(!lease.Fresh(3102) && !lease.Fresh(99));
    Wai::ClientIdentity identity;
    assert(identity.Parse("0123456789abcdef 87 83 81 69"));
    identity.heartbeat = 100;
    assert(identity.Fresh(3100) && !identity.Fresh(3101) && !identity.Fresh(99));
    assert(!identity.Parse("0123456789abcdef 87 87 81 69"));
    assert(!identity.Parse("invalid 87 83 81 69"));
    assert(identity.marker == "0123456789abcdef");
    using namespace ControlModeCommand;
    assert(Parse("mode") == Request::Status && Parse("mode ?") == Request::Status);
    assert(Parse("mode observation") == Request::Observation);
    assert(Parse("mode action") == Request::Action);
    assert(Parse("mode action extra") == Request::Invalid && Parse("follow") == Request::Unrelated);
}
