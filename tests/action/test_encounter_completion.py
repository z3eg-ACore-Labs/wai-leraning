"""Compile actual encounter mask derivation and completion hook; no server or database."""
import argparse
from pathlib import Path
import os
from compile_fixture import compile_and_run

FIXTURE = r'''
#include "PassiveRun.h"
#include <cassert>
#include <algorithm>
#include <initializer_list>
#include <iostream>
#include <list>
#include <map>
#include <mutex>
#include <vector>
using uint32=unsigned;using uint64=std::uint64_t;using Difficulty=unsigned;using EncounterCreditType=unsigned;
struct ObjectGuid{uint64 raw;explicit ObjectGuid(uint64 v):raw(v){} uint64 GetRawValue(){return raw;}};
struct DungeonEncounterEntry{unsigned encounterIndex;};
struct DungeonEncounter{DungeonEncounterEntry const* dbcEntry;unsigned lastEncounterDungeon;};
using DungeonEncounterList=std::list<DungeonEncounter const*>;
struct InstanceScript{unsigned mask=0;unsigned GetCompletedEncounterMask(){return mask;}};
struct Map{
 unsigned id=33,instance=7;bool raid=false,dungeon=true;InstanceScript script;
 bool IsDungeon(){return dungeon;}bool IsRaid(){return raid;}unsigned GetInstanceId(){return instance;}
 unsigned GetId(){return id;}Map* ToInstanceMap(){return this;}InstanceScript* GetInstanceScript(){return &script;}
};
struct Player;struct Creature;
struct Unit{
 Map* map=nullptr;Player* actor=nullptr;virtual Creature* ToCreature(){return nullptr;}
 Map* GetMap(){return map;}Player* GetCharmerOrOwnerPlayerOrPlayerItself(){return actor;}
};
struct Player:Unit{uint64 guid=42;bool eligible=true;ObjectGuid GetGUID(){return ObjectGuid(guid);}};
struct Creature:Unit{bool owned=true;Creature* ToCreature() override{return this;}};
enum{GLOBALHOOK_ON_AFTER_UPDATE_ENCOUNTER_STATE};
struct GlobalScript{
 GlobalScript(char const*,std::initializer_list<int>){}
 virtual void OnAfterUpdateEncounterState(Map*,EncounterCreditType,uint32,Unit*,Difficulty,DungeonEncounterList const*,uint32,bool){}
};
struct Watch{uint64 heartbeat=100;std::map<unsigned,Wai::PassiveRun> runs;};
std::map<uint64,Watch> passiveWatches;std::mutex mutex;bool stopping=false;
struct Lease{bool active=false;}lease;
uint64 tick=100;uint64 Now(){return tick;}Player owner;
namespace ObjectAccessor{Player* FindPlayer(ObjectGuid){return &owner;}}
bool EligibleHuman(Player* p){return p && p->eligible;}
bool OwnLoot(Player*,Creature* c){return c->owned;}
std::vector<Player*> Party(Player* p){return {p};}
'''
TESTS = r'''
int main(){
 DungeonEncounterEntry a{0},b{1},c{2},invalid{32};
 DungeonEncounter ea{&a,0},eb{&b,0},ec{&c,99},bad{&invalid,99};
 DungeonEncounterList definitions{&ea,&eb,&ec};
 assert(RequiredEncounters(&definitions)==7);assert(RequiredEncounters(nullptr)==0);
 DungeonEncounterList noFinal{&ea,&eb};assert(RequiredEncounters(&noFinal)==0);
 DungeonEncounterList malformed{&ea,&bad};assert(RequiredEncounters(&malformed)==0);
 HumanEncounters hook;Map map,foreign;owner.map=&map;Creature source;source.map=&map;
 auto& run=passiveWatches[42].runs[7];run.map=33;run.difficulty=1;run.members={42};
 map.script.mask=7;source.owned=false;
 hook.OnAfterUpdateEncounterState(&map,0,0,&source,1,&definitions,99,true);assert(!run.Complete());
 source.owned=true;hook.OnAfterUpdateEncounterState(&map,0,0,&source,0,&definitions,99,true);assert(!run.Complete());
 source.map=&foreign;hook.OnAfterUpdateEncounterState(&map,0,0,&source,1,&definitions,99,true);assert(!run.Complete());
 source.map=&map;map.script.mask=4;
 hook.OnAfterUpdateEncounterState(&map,0,0,&source,1,&definitions,99,true);assert(!run.Complete());
 map.script.mask=7;hook.OnAfterUpdateEncounterState(&map,0,0,&source,1,&definitions,0,true);assert(run.Complete());
 hook.OnAfterUpdateEncounterState(&map,0,0,&source,1,&definitions,99,true);assert(run.completedMask==7);
 run={};run.map=33;run.difficulty=1;
 hook.OnAfterUpdateEncounterState(&map,0,0,&source,1,&definitions,99,false);assert(!run.Complete());
 Unit spell;spell.map=&map;Player stranger;stranger.guid=99;spell.actor=&stranger;
 hook.OnAfterUpdateEncounterState(&map,1,0,&spell,1,&definitions,99,true);assert(!run.Complete());
 run.members={42};spell.actor=&owner;
 hook.OnAfterUpdateEncounterState(&map,1,0,&spell,1,&definitions,99,true);assert(run.Complete());
 run={};run.map=33;run.difficulty=1;tick=4000;
 hook.OnAfterUpdateEncounterState(&map,0,0,&source,1,&definitions,99,true);assert(run.interrupted);
 tick=100;run={};run.map=33;run.difficulty=1;lease.active=true;
 hook.OnAfterUpdateEncounterState(&map,0,0,&source,1,&definitions,99,true);assert(run.interrupted);
 std::cout<<"Production encounter hook: required masks, final credit, ownership, spell credit, difficulty, foreign map, duplicate, expiry passed\n";
}
'''


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--compiler',default=os.environ.get('CXX', 'c++'))
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();root=Path(__file__).resolve().parents[2]
    source=(root/'src/action/WaiAction.cpp').read_text()
    mask=source[source.index('uint32 RequiredEncounters('):source.index('std::vector<Player*> Party(')]
    hook=source[source.index('class HumanEncounters :'):source.index('class Input :')]
    output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    fixture=output/'encounter-completion.cpp';fixture.write_text(FIXTURE+mask+hook+TESTS)
    compile_and_run(args.compiler, root, fixture)


if __name__=='__main__':main()
