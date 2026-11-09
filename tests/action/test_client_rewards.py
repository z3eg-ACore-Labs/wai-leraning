"""Compile the production client reward hook against deterministic core doubles."""
import argparse
from pathlib import Path
import os
from compile_fixture import compile_and_run


FIXTURE = r'''
#include "ClientProgress.h"
#include <atomic>
#include <cassert>
#include <initializer_list>
#include <iostream>
#include <mutex>
#include <set>
using uint32=unsigned;
struct Player; struct Creature;
struct Unit {
    unsigned map=389, instance=7, guid=1;
    virtual Player* ToPlayer(){return nullptr;}
    virtual Creature* ToCreature(){return nullptr;}
    unsigned GetMapId(){return map;} unsigned GetInstanceId(){return instance;}
    unsigned GetGUID(){return guid;}
};
struct Player:Unit {Player* ToPlayer() override{return this;}};
struct Creature:Unit {
    unsigned entry=11517;bool owned=true;
    Creature* ToCreature() override{return this;} unsigned GetEntry(){return entry;}
};
enum {UNITHOOK_ON_UNIT_DEATH};
struct UnitScript {
    UnitScript(char const*,bool,std::initializer_list<int>){}
    virtual void OnUnitDeath(Unit*,Unit*){}
};
std::atomic<unsigned> activeOwner{1};std::mutex mutex;bool clientMode=true;
unsigned clientMapId=389,instanceId=7,ownerGuid=1;
Wai::ClientProgress clientProgress;std::set<unsigned> clientMembers{1,2};
struct Lease {bool active=true;bool Fresh(unsigned){return active;}} lease;
unsigned Now(){return 1000;}
Player owner;
namespace ObjectAccessor {Player* FindPlayer(unsigned){return &owner;}}
bool OwnLoot(Player*,Creature* creature){return creature->owned;}
#define LOG_INFO(...) (void)0
'''
TESTS = r'''
int main() {
    ClientRewards hook;Creature boss;
    activeOwner=0;hook.OnUnitDeath(&boss,nullptr);assert(clientProgress.Count()==0);
    activeOwner=1;boss.instance=8;hook.OnUnitDeath(&boss,nullptr);assert(clientProgress.Count()==0);
    boss.instance=7;boss.map=1;hook.OnUnitDeath(&boss,nullptr);assert(clientProgress.Count()==0);
    boss.map=389;boss.owned=false;hook.OnUnitDeath(&boss,nullptr);assert(clientProgress.Count()==0);
    boss.owned=true;hook.OnUnitDeath(&boss,nullptr);hook.OnUnitDeath(&boss,nullptr);
    assert(clientProgress.Count()==1);
    for (unsigned entry:Wai::ClientProgress::BossEntries){boss.entry=entry;hook.OnUnitDeath(&boss,nullptr);}
    assert(clientProgress.Count()==4);
    Player other;other.guid=99;hook.OnUnitDeath(&other,nullptr);assert(clientProgress.deaths==0);
    other.guid=2;hook.OnUnitDeath(&other,nullptr);assert(clientProgress.deaths==1);
    clientMode=false;hook.OnUnitDeath(&other,nullptr);assert(clientProgress.deaths==1);
    clientMode=true;lease.active=false;hook.OnUnitDeath(&other,nullptr);assert(clientProgress.deaths==1);
    std::cout << "Production client rewards: exact instance, party deaths, ownership and unique bosses passed\n";
}
'''


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--compiler',default=os.environ.get('CXX', 'c++'))
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[2]
    source=(root/'src/action/WaiAction.cpp').read_text()
    hook=source[source.index('class ClientRewards :'):source.index('class HumanRewards :')]
    output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    fixture=output/'client-rewards.cpp';fixture.write_text(FIXTURE+hook+TESTS)
    compile_and_run(args.compiler, root, fixture)


if __name__=='__main__':main()
