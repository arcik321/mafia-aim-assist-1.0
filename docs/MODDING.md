# Mafia 1.0 Modding Notebook

## Sources And Evidence

Primary reference: https://github.com/st0rm94/mafia-reverse-engineering-export .
Use its PC bodies and struct documents, then verify against the exact local executable.
Never transfer a PS2 offset or a 1.3 address into this build without a separate check.
Prefer this notebook, README's address table and the machine-readable profile before another web search.

Evidence labels for future additions:
- RE: a body/caller or shipped script establishes the claim.
- Binary: local executable bytes, receiver setup and stack cleanup agree.
- Harness: isolated x86 regression test passes; not proof of full engine behavior.
- Runtime: observed in the running game. Record model, mission and relevant log.

The user confirmed aim, Xbox B toggle, Little Italy tutorial launch and cigarette animation.
Temporary-target acquisition has runtime aim-correction logs. The user subsequently reported crowd collapse,
frozen/unkillable ambient gangsters and a fleeing custom mafioso in the previous build. The revised panic and
damage-alert paths have harness coverage, but their visible behavior still needs confirmation in a fresh session.
Earlier tests missed temporary actor registration and misidentified the Tutorial action.

## Supported Build And Tools

Active private repo: https://github.com/arcik321/mafia-aim-assist-1.0 .
Local source: D:\MafiaAimAssist-1.0. Game: D:\SteamLibrary\steamapps\common\Mafia10\Mafia.
The public PC 1.3 source/downloads are separate. Keep original worktrees and prototype files intact.
Do not stage token files, game executables, archives of game assets, or build products.

The existing MSVC Win32 compiler is sufficient. No new compiler is required.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/check-game.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/cleanbuild.ps1 -Test
```

The checker parses PE sections, checks the executable hash/architecture/base and validates stored symbol bytes.
It checks the original disk EXE, not runtime-patched memory. The profile is a verified reference, not yet a
generated C binding layer. A new symbol must record RVA, receiver, argument types, return ABI, cleanup and evidence.
Do not invent signatures from a function name alone.

Logs: %TEMP%\MafiaAimHost.log and MafiaAimLogic.log. Archive before a clean runtime experiment.
Actual local disassembly is currently %TEMP%\mafia10_disasm.txt; regenerate with MSVC dumpbin when absent.
Selected RE documents may be cached locally. Do not depend on machine-specific temporary caches for a release.

## World And Actor Ownership

- Mission pointer RVA 0x25115C; mission+0x24 is C_game, not the player.
- C_game+0xE4 is the player; +0x50 is the camera frame; byte+0x40 is the running guard.
- Mission+0x38/+0x3C is the placed actor pointer vector.
- C_game+0x124/+0x128 is the temporary actor vector. Spawned actors can exist only here.
- Aim acquisition, switching and retention must use BOTH vectors and deduplicate.
- Kind+0x10: player=2, human=3, scripted NPC=0x1B, pedestrian generator=0x12.
- Ambient pedestrians are C_traffic_element records, not C_entity actors. They have no actor kind/vtable and
  do not appear in the actor vectors. Full ambient-pedestrian lock-on needs a separate crowd adapter; never cast
  a pedestrian record as a scripted NPC. Current lock-on covers living C_human/C_entity actors including spawns.
- Frame vtable methods used here are COM-style stdcall calls with self on the stack. Actor methods are thiscall.
- Game::AddTemporaryActor invokes entity GameInit and owns session cleanup. Do not free a registered actor twice.
- Use instance-only vtable overrides for custom NPC AI. A class vtable patch changes every NPC of that class.

## ABI Lessons

- Verify the FULL function entry. The old 1.3 crouch crash skipped sub esp,18h by entering three bytes late.
- Native bool returns use AL: declare a byte return, not int. Upper EAX bits are not a boolean result.
- A native float return can use x87 ST0. Do not read it as EAX.
- PC 1.0 LOS thunk needs ECX=collision object RVA0x27A588 plus six stack arguments and ret0x18.
- Do_Crouched has ECX=human, one bool stack argument and ret4.
- Do_PlayAnim has name and two boolean stack arguments and ret0x0C.
- C_entity's embedded script VM starts at +0xA9C. GetProgram returns a vector header at +0xAA8, not the VM base.
- Test byte signatures and callers. A field writer does not by itself establish a safe callable API.

## Tutorial Sandbox

Actual Tutorial action is 0x11, result instruction RVA0x176AA7, dispatch RVA0x1FA260 -> VA0x5F9C8A.
That branch opens literal tutorial at VA0x651338. Action0x16 is an intro branch, not Tutorial.
The host redirects to native FreeItaly action0x1C and keeps native world/player/car setup.
Police traffic ratio is zero; scripted police activation is suppressed only during this session.
The session flag is cleared after the native game loop returns. Other story/Free Ride sessions keep native police.

Gunfire can trigger native pedestrian mega-panic/collapse. This is not proof that everyone died.
Only the two mega-panic calls in the ambient update are skipped in this sandbox. Do not substitute SetPanic
at those sites: it has separate path/witness/latch guards and is not an equivalent state transition.
The regression harness invokes both patched native caller layouts to exercise ECX, stack arguments and cleanup.
The Hit dispatcher is untouched; actual injury/death behavior is not replaced or resurrected.

## NPC Creation And State

Current native sequence:
1. Driver global RVA0x2F9520 creates a model frame of type9.
2. Model cache object RVA0x2F9418 loads it through Open RVA0x048940; check its status contract.
3. Name and link frame into scene; set position and heading before actor Init.
4. Mission::CreateActor(kind0x1B), actor Init(vtable+0x48), set human_type+0x5FC BEFORE GameInit.
5. Game::AddTemporaryActor computes AI group and initializes perception/action state.
6. Add inventory item and rebuild weapon model. Current smoking enemy uses a Colt item {9,7,35,0,0,0}.
7. Bind only this instance's AI, then apply idle behavior and its attachment.

Current model SamHIGH.i3d resolves to installed SamHIGH.4ds. The enemy spawns six metres ahead facing away.
Animations KoureniAutoStativ.i3d and KoureniAutoPotahnuti.i3d are verified from shipped MISE04-SALIERY scripts.
The 2cigaro.i3d model is linked at the right-hand weapon attachment +0x570 and hidden on combat transition.

Before combat, suspend the native state switcher, but keep visual contacts refreshed via ai_vis_logs_tick.
The initial visual alert requires the player in front AND native ai_sensors_can_see(entity+0xBB4, player).
Nonfatal health loss also ends smoking regardless of facing, including after an earlier alert. This is a damage
signal, not verified attacker identification; other damage sources also wake him. Sound alone does not wake him.
The custom enemy alone has effective aggression=1 and morale=1; morale dampens the native fear score.
Once alerted, stop smoking and release the switcher. Do NOT force state3 with an empty angry target (+0x1280):
the suspended selector has not populated it. Let native perception/logs/state selection initialize combat.
No health, death flags, global actor state or other NPC personality is reset by this path.
Aggression is not the same as faction or visibility. The actual instance-vtable callback, native stop/suspend
ABIs, forwarding for unrelated actors and fatal-damage behavior are exercised by the x86 harness.

Future spawn APIs must handle ground placement, collision clearance, model/init failure rollback, ownership,
dead/despawned actor handles, cooldowns and session reset. Current placement is relative to player origin;
robust terrain/capsule validation is still a next step, not a completed general-purpose spawn framework.

## Personality And Editor Parameters

Verified references: structs/human.md, ai_character.md, ai_sensors.md, ai_logs.md and ai_fight_modes.md in RE.
C_human has 15 named property slots: Strenght, Energy, EnergyHandL/R, EnergyLegL/R, Reactions, Speed,
Aggresivity, Intelligence, Shooting, Sight, Hearing, Driving and Mass. Preserve the original misspellings in scripts.
Maximum values begin at +0x600; current values at +0x640. Script human_setproperty uses percent of the maximum,
not an absolute raw float. Do not write an editor percentage directly into an internal 0..1-style stat.

C_entity+0xEBC is ai_character. Each effective stat is base property plus its per-instance delta:

| Stat | Base field | Delta in ai_character | Getter RVA | Effect |
| --- | --- | --- | --- | --- |
| Aggression | +0x660 | +0x10 | 0x032520 | Scales anger by aggression+0.5; not a hostility switch |
| Intelligence | +0x664 | +0x04 | 0x032510 | Biases combat mode scoring toward crouch/cover |
| Morale | +0x67C | +0x08 | 0x032500 | Low morale increases fear; no named slot15 script property |
| Sight | +0x66C | +0x14 | 0x032530 | Range=sight*200 plus visual-contact sensitivity |
| Hearing | +0x670 | +0x18 | 0x032540 | Loudness threshold=1.05-hearing |

The getters add values without a verified clamp. 0..1 is a conservative tuning interval, not an enforced API
range. Validate preset behavior in the game; extreme values can undermine perception/combat assumptions.
Morale lives outside the 15 named properties. Four other AI delta slots are unused; do not label them as features.

Role is set by human_type before GameInit: 0x10 -> group4 (mission enemy), 0x2 -> group0 (often companions),
0x80 -> group1 (civilian), 0x4 -> group2 (police). Roles are per-scene, not the character model's identity.
An ally role alone does not implement follow, protect, friendly-fire policy or mission dialogue.

Proposed presets (not implemented or calibrated yet): nervous lookout, aggressive thug, tactical gunman,
quiet guard and allied bodyguard. Combine personality, senses, weapon/accuracy/health, faction and an idle script.

## Dynamic Features Next

### Driver Police Aim

`vehicle_aim=1` enables driver-seat-only LT/O lock-on. GetActiveWorld permits cars, while GetWorld remains
on-foot-only for B crouch (B can therefore be the brake). No shot/AI/physics setter is called by this feature.
The target order is police wheels, scripted foot cops (group2), then ambient cops (categories1/3), choosing
the best geometrically reachable officer across both officer pools. Civilians and seated actors are excluded.
Only targets in the driver-left 40..140 degree sector, elevation -30..25 degrees and range80m qualify.
Wheels and distant officers use camera cone20 degrees. Nearby foot officers (weapon distance<=8m) use the
full reachable left sector so standing at the door does not require already pointing the camera at them.
The view and firing line are checked on every correction, with no on-foot LOS grace period.

RE and local binary getter evidence: wheel count C_car+5B0, pointer array+D24; wheel world hub+1C,
radius+10C, flags+120. Skip terminal flag40000000 without claiming that every punctured tyre has that flag.
Police-car flag C_car+2044 is used rather than model names. Ambient vector globals RVA2560C4/2560C8,
record stride200, active+C, position+10, category+15A. These records are never cast to C_human.

Native driver code VA4C9C8F and4CA326 reads mission+10 scene, scene+17C active camera, camera+30 direction
and+40 position. Use that camera for angular correction: human+200 is only latched while shooting and is NOT
a live look direction. Weapon frame human+564 supplies firing clearance. The weapon line must always be clear;
only nearby officers omit the chase-camera ray, which can intersect the player's own car while the gun is clear.
Their endpoint tolerance is0.35m to avoid mistaking the target's body capsule for cover before its chest point.
Officer aim height is1.25m (upper torso); minimum ray distance0.25m admits officers directly at the driver's door.
Wheels and distant officers retain both rays. Failed collision binding/invalid frame fails closed even if ordinary
LOS is disabled in the INI. Tests cover both officer pools, body-surface hits, camera blocked/gun clear,
gun blocked, distant cone retention and right-side rejection. This does not disable native carjack/arrest AI.
Wheel line tolerance uses its radius+0.02; this is a geometric approximation, not an identified hit-mesh guarantee.
Spread, motion, stale engine poses and input timing mean this feature cannot promise every bullet hits a tyre.
Drive-by gains are separate from the persisted foot calibration; driver motion does not train foot sensitivity.
In-game correction/framing still requires runtime confirmation. The experimental sandbox is now off by default
(`[sandbox] enabled=0`), following the user's decision to abandon its unresolved explosion/crash investigation.

- Button spawn: rising edge queues a typed spawn request; a main-thread update drains it after checking the world.
- Ground-shot allies: observe the player's real shot and its collision hit, check downward direction and ground
  normal/range, debounce one trigger per shot, then queue allies around a validated point.
- Preserve original events and callbacks. A single game script-event slot is not a general event bus; replacing
  it could break mission logic. Hook a verified producer and forward original behavior instead.
- Do not create actors from an arbitrary XInput/watch thread or execute a native setter from the mouse hook.
- Use bounded commands, cooldowns, one spawn per requested edge and generation-aware handles for stale actors.
- Configuration/data hot reload is useful now. Host-hook/native ABI changes require a restart. Existing DLL reload
  is not a fully synchronized production framework; extend ownership and in-flight lifetime rules before expanding it.

## Low-Cost Development Workflow

1. Read the local relevant wrapper/profile/neighboring test, not the whole RE repository.
2. State one behavior hypothesis and its cheapest falsifying check; use one targeted RE read if unresolved.
3. Verify new native receiver/arguments/return/cleanup and a real caller in this exact EXE.
4. Add the fact to the profile/notebook with evidence status, then implement a small scoped change.
5. Run check-game and the focused x86 harness before deployment. Keep tests aligned with actual native boundaries.
6. Back up DLLs/config and archive logs. Never overwrite the host while the game is running.
7. Distinguish installed hook, executed callback, runtime behavior and visible presentation in reports.
8. Commit code and evidence notes together. Do not push or publish private work unless requested.

Useful next infrastructure: one typed game API module generated from this catalog, declarative NPC presets,
a main-thread command queue, a debug console/overlay and a small registry of tested model/animation pairs.
An editor or Ghidra project can complement this, but deterministic bindings, tests and local notes deliver the
largest immediate reduction in repeated AI reasoning. Never promise zero RE or zero tokens for new engine behavior.