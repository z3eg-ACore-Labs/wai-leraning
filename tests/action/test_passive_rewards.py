"""Compile the actual passive reward hook against deterministic core doubles."""
import argparse
from pathlib import Path
import os
from compile_fixture import compile_and_run

FIXTURE = r'''
#include "PassiveRun.h"
#include <atomic>
#include <cassert>
#include <initializer_list>
#include <iostream>
#include <map>
#include <mutex>
using uint32=unsigned; using uint64=std::uint64_t;
struct ObjectGuid {uint64 raw; explicit ObjectGuid(uint64 v):raw(v){} uint64 GetRawValue(){return raw;}};
struct Player; struct Creature;
struct Unit {
    unsigned map=389, instance=7;uint64 guid=99;
    virtual Player* ToPlayer(){return nullptr;} virtual Creature* ToCreature(){return nullptr;}
    unsigned GetMapId(){return map;} unsigned GetInstanceId(){return instance;}
    ObjectGuid GetGUID(){return ObjectGuid(guid);}
};
struct Player:Unit {bool eligible=true;Player* ToPlayer() override{return this;}};
struct Creature:Unit {
    unsigned entry=11517;bool owned=true;
    Creature* ToCreature() override{return this;} unsigned GetEntry(){return entry;}
};
enum {UNITHOOK_ON_UNIT_DEATH};
struct UnitScript {UnitScript(char const*,bool,std::initializer_list<int>){} virtual void OnUnitDeath(Unit*,Unit*){}};
std::atomic<bool> clientEnabled{true};unsigned clientMapId=389;std::mutex mutex;bool stopping=false;
struct Lease {bool active=false;} lease;
struct PassiveWatch {uint64 heartbeat=100;std::map<unsigned,Wai::PassiveRun> runs;};
std::map<uint64,PassiveWatch> passiveWatches;
uint64 tick=100;uint64 Now(){return tick;}
Player owner;
namespace ObjectAccessor {Player* FindPlayer(ObjectGuid){return &owner;}}
bool OwnLoot(Player*,Creature* creature){return creature->owned;}
bool EligibleHuman(Player* player){return player && player->eligible;}
'''
TESTS = r'''
int main() {
    auto& run=passiveWatches[42].runs[7];run.members={1,2};
    HumanRewards hook;Creature boss;
    boss.instance=8;hook.OnUnitDeath(&boss,nullptr);assert(run.progress.Count()==0);
    boss.instance=7;boss.map=1;hook.OnUnitDeath(&boss,nullptr);assert(run.progress.Count()==0);
    boss.map=389;boss.owned=false;hook.OnUnitDeath(&boss,nullptr);assert(run.progress.Count()==0);
    boss.owned=true;hook.OnUnitDeath(&boss,nullptr);hook.OnUnitDeath(&boss,nullptr);assert(run.progress.Count()==1);
    Player member;member.guid=99;hook.OnUnitDeath(&member,nullptr);assert(run.progress.deaths==0);
    member.guid=2;hook.OnUnitDeath(&member,nullptr);assert(run.progress.deaths==1);assert(!run.Complete());
    for(unsigned entry:Wai::ClientProgress::BossEntries){boss.entry=entry;hook.OnUnitDeath(&boss,nullptr);}
    assert(run.Complete());hook.OnUnitDeath(&member,nullptr);assert(run.progress.deaths==1);
    run={};lease.active=true;hook.OnUnitDeath(&boss,nullptr);assert(run.interrupted);assert(!run.Complete());
    lease.active=false;run={};owner.eligible=false;hook.OnUnitDeath(&boss,nullptr);assert(run.progress.Count()==0);
    owner.eligible=true;run={};tick=4000;hook.OnUnitDeath(&boss,nullptr);assert(run.interrupted);
    tick=100;run={};owner.instance=8;hook.OnUnitDeath(&boss,nullptr);assert(run.progress.Count()==0);
    run={};run.map=33;
    assert(!run.Encounter(7,4,true,true,true));assert(!run.Complete());
    assert(run.Encounter(7,7,false,true,true));assert(run.Complete());
    run={};run.map=33;assert(!run.Encounter(7,7,true,false,true));assert(!run.Complete());
    assert(!run.Encounter(7,7,true,true,false));assert(!run.Complete());
    assert(!run.Encounter(0,7,true,true,true));assert(!run.Complete());
    std::cout << "Passive production hook: instance, ownership, unique bosses, wipe, expiry and controller exclusion passed\n";
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--compiler', default=os.environ.get('CXX', 'c++'))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = (root/'src/action/WaiAction.cpp').read_text()
    hook = source[source.index('class HumanRewards :'):source.index('// Existing core encounter credits')]
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=True)
    fixture = output/'passive-rewards.cpp'; fixture.write_text(FIXTURE+hook+TESTS)
    compile_and_run(args.compiler, root, fixture)


if __name__ == '__main__': main()
