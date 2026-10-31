# Particle snapshot state inventory

Audit date: 2026-10-30. This describes the implementation and remaining gaps;
it is **not** a claim of complete particle/world restoration. A successful
player-trajectory comparison cannot validate particle rendering or emission.

## Ownership and snapshot boundary

`GeneralParticleSystem` owns a `ParticleEmitter` through local parameter 0.
Its activity and ordinary emission accumulator are local parameters 1 and 2.
The enclosing game snapshot must restore those parameters, the behavior
scheduler, entity transforms, attributes, RNG and clock as well as this module's
opaque snapshot. The module does not own the engine context or referenced CK
objects. Emitter/behavior addresses are lifetime identity checks, not portable
serialization. Current restoration assumes the captured emitter survives.

## Emitter fields

| Fields | Meaning / source | Current handling and remaining work |
| --- | --- | --- |
| `particles`, `m_Pool`, `m_BackPool`, `m_MaximumParticles` | Active/free lists and allocated storage | Pool pointers become indices; restore uses current allocation. Since E1909, whole-snapshot preflight rejects capacity changes and missing/replaced emitters before mutation. Destructor invalidation also rejects same-address emitter reuse. Rehydration remains unimplemented. |
| `totalParticles`, `particleCount`, `emitsPerFrame`, `emitsVar` | Emission limits and counters | Captured explicitly. Some limits are refreshed by `ReadInputs`; count/list consistency still matters before that call. |
| `m_DeltaTime`, `m_Active` | Last update step and emission enable | Captured explicitly. These do not encode draw-callback registration. |
| `m_Flag0x10C` | First timed emission initialization | Captured since E1901. Reconstructing from particle count is incorrect. |
| `m_IsTimePointEmitter`, `m_IsWaveEmitter` | Constructed emitter kind | Recorded to select typed histories; object identity is retained. No vtable or emitter object memcpy. |
| TimePoint `m_Position`, `m_Time` | Previous emission position and accumulated elapsed time | Captured since E1901. Used by `AddParticles2`, including interpolation and emission count. |
| TimePoint `m_EmissionDelay` | Cached input (despite name, multiplied by elapsed seconds in implementation) | Captured explicitly since E1934; constructor initializes zero before normal typed ReadInputs. Units unchanged. |
| Wave `m_Position`, `m_Angle` | Previous motion and angle used when stationary | Captured since E1901. Used by `AddParticles3`. |
| `m_Entity`, `m_Context` | Owner identity / context | E1934 captures entity reference, validates context and resource ID/address before writes. Emitter destructor invalidation remains; missing owners are rejected, not reconstructed. Resource ID/address is not a generation token. |
| `m_Texture`, `m_Group`, `m_Mesh`, `m_MessageType` | Referenced resources, display mesh and collision-message settings | Captured fieldwise since E1934; resource identities checked before writes. E1940/E1942 cover destroyed/pending-deletion resources and two-emitter preflight atomicity. Generic E1941 also rejects pending deletion; E1942 covers both paths. ID/address ABA remains unresolved. |
| `m_RenderParticlesCallback` | Cached renderer function selected by settings | Captured since E1934; initialized to sprite callback before settings load. Registration remains separate; see below. E1935 checks fresh ShowParticles selection. |
| `m_YawVariation`, `m_PitchVariation`, `m_Speed`, `m_SpeedVariation`, `m_AngularSpeed`, `m_AngularSpeedVariation` | Cached kinematic inputs | Captured fieldwise since E1934; conditional ReadInputs is no longer relied on to restore these caches. |
| `m_StartSize`, `m_StartSizeVar`, `m_EndSize`, `m_EndSizeVar`, `m_Life`, `m_LifeVariation` | Cached size/lifetime inputs | Captured fieldwise since E1934. E1935 exercises future particle size/life outputs. |
| `m_Weight`, `m_WeightVariation`, `m_Surface`, `m_SurfaceVariation`, `m_Bounce`, `m_BounceVariation` | Cached interactor/deflector inputs | Captured fieldwise since E1934; not all interactor/deflector combinations have dedicated tests. |
| `m_StartColor`, `m_StartColorVar`, `m_EndColor`, `m_EndColorVar` | Cached colors | Captured fieldwise since E1934; E1935 checks subsequent emission color values, not full visual output. |
| `m_InitialTextureFrame`, `m_InitialTextureFrameVariance`, `m_SpeedTextureFrame`, `m_SpeedTextureFrameVariance`, `m_TextureFrameCount`, `m_TextureFrameloop` | Cached texture animation settings | Captured fieldwise since E1934; texture-animation visual validation remains. |
| `m_SrcBlend`, `m_DestBlend`, `m_EvolutionsFlags`, `m_VariancesFlags`, `m_InteractorsFlags`, `m_DeflectorsFlags`, `m_RenderMode` | Cached local settings | Captured fieldwise since E1934; blend defaults are initialized before ReadSettings. The fixture covers representative flags and callback selection, not every combination. |
| `m_Impacts`, `m_CurrentImpact` | Collision outputs awaiting consumption | Values/cursor captured; E1934 preflight rejects missing/replaced impact objects before writes instead of silently substituting null. E1941 also rejects pending deletion; E1942 covers both paths. ID/address ABA remains unresolved. |
| ObjectEmitter `m_Shape` | Current entity mesh | Typed reference captured since E1934. E1941 also rejects pending deletion; E1942 verifies both pending and actual destruction. World owns resource lifetime. |
| CurveEmitter `m_Curve` | Curve owner reference | Typed reference captured since E1934. E1941 also rejects pending deletion; E1942 verifies pending/actual destruction. Missing owner is rejected; not reconstructed. |
| Point/Planar/Cubic/Line/Disc/Cylindrical/Spherical derived classes | Shape-specific methods | No additional data members in the inspected headers. Their input entity transforms/mesh/geometry still belong to the world snapshot. |

Since E1952 every slot's non-pointer object representation is retained as
unsigned bytes, not as a typed Particle value. Conditional texture/interactor/
deflector fields may retain earlier slot history, so free payload is neither
dropped nor zeroed. Only the snapshot representation changes; the allocator,
RNG and actual emission code are unchanged. This is an in-process checkpoint,
not a cross-platform or on-disk serialization format.

Active/free lists must form a disjoint complete pool partition. Capture rejects
cycles, out-of-range/misaligned pointers, count mismatch, broken active backlinks
and orphaned slots. It evaluates active prev and all next links only after
checking each node's range. Free prev is never read; allocation overwrites it
before use. Restore reconstructs links from indices and sets unused free prev
null. Static assertions require Particle to remain trivially copyable and
standard-layout with adjacent link fields; object payload is copied by memcpy.
Defined-value behavior is tested, not inferred from a zero-initialized game pool.

## Rendering and manager state

`ShowParticles(false)` removes the emitter's post-render callback. It is called
when an inactive system becomes empty and on script deactivation/pause.
`ShowParticles(true)` removes the known particle callbacks, then adds the selected
one; it can also update the editor/display mesh. `TT_ParticleRestoreSnapshot`
still does neither on its own. E1905b proved a registered callback remained absent
after particle-only restoration. E1907/E1909 now coordinate a separate renderer
callback snapshot in PracticeTools, preserving registration, ordering and temporary
cleanup queues. Both renderer-owner and particle-emitter lifetimes are validated
before world mutation. Callback argument owners outside this explicit coverage
must remain alive; arbitrary opaque pointers are not universally validated.

Do not infer registration solely from `m_Active` or particle count: script
activation/pause and first emission have distinct phases. Do not remove/re-add
callbacks during capture to probe presence: it changes ordering. A solution must
observe and restore registration/order without capture-time mutation, respect
other callbacks and temporary callback bookkeeping, and validate callback-owner
lifetimes. The enclosing snapshot also needs to restore cached render settings.

`ParticleManager` fields:

- Attribute IDs (`m_*Attribute`) are registrations, not per-frame numerical
  state. Object memberships/attribute values belong to the engine snapshot.
- `m_*Mesh` IDs refer to generated display resources. Preserve ownership and
  object identity; do not copy pointer bytes or regenerate during every rewind.
- `m_ShowInteractors` affects display and defaults on subsequently attached
  emitters. E1934 captures/restores it directly, without ShowInteractors side effects. Manager destruction invalidates handles; E1946/E1947 verify same-address manager replacement over three lifetimes (27 checks). Resource registrations/count are a separate ownership question.
- `m_TotalParticleCount` counts created emitters (despite its name); creation and
  deletion update it. Current snapshot does not recreate missing emitters.
- Static `m_GlobalIndices` and count are capacity-managed draw index caches;
  draw routines regenerate their contents when growing. They are not a logical
  emission clock. Scratch `g_Min/g_Max/g_Delta/g_One` is overwritten by relevant
  update/setup paths; do not snapshot allocation addresses as game state.

## Verified scope

- E1900d: unchanged valid real-engine fixture, 55/90 future emission observations
  differ after restore. Earlier a/b/c fixture mistakes are retained separately.
- E1901: same fixture, 90/90 observations match after temporal-history fix.
  Covers save before initial emission, mid-emission, two slots and three restores
  per slot. It uses real AddParticles2/3, CK objects and snapshot API.
- E1904: `test_particle_snapshot` builds/runs through the repository CMake stanza
  against qualified imported runtime libraries, without a window.
- E1902/E1903: normal SR06 trace remains neutral; two saves/four restores produce
  56,822 matching player records and zero collision-graph differences. This is
  not validation of every particle/cache/renderer field above.
- E1905b: draw-registration regression fails in all three initialized cases.
  This is the preserved failing control; E1907c fixes the three cases through
  coordinated renderer/particle restoration, not through the particle API alone.

- E1910: 90 emission observations pass, plus three same-address emitter
  destruction/recreation cases reject expired snapshots without changing the
  replacement emitter. E1913 runs the retained fixtures through CTest.
- E1914c: 79 renderer checks pass, including exact owner address/ID reuse,
  callback ordering, temporary cleanup and mesh/context destruction.
- E1912/E1916: SR06 repeated rollback passes 56,822 player records and four
  collision comparisons. E1916 observes life UI clones losing parameter/array/
  group references after rollback while remaining in the scene. General object
  lifecycle, cached settings and complete-world fidelity remain unresolved.

- E1921/E1922 subsequently fix the audited new life UI remnant through
  dependency-checked ordinary-object rollback in PracticeTools. This removes
  newly created objects, but does not rehydrate destroyed captured owners or
  complete world ownership or manager-resource lifecycle coverage.

- E1933b corrected old-library control fails105/108 observations. The earlier
  E1933 fixture did not reset each slot's baseline caches consistently; that
  failure and source are retained rather than treated as final evidence.
- E1934 captures caches explicitly without emitter layout/vtable changes; manager
  identity/destruction and referenced resources are preflight checked. It does
  not recreate missing emitters, restore manager resource registrations/counts,
  solve generic object ABA or make whole-world restoration atomic.
- E1935 all three repository tests pass: 90 history observations, three coordinated
  callback cases, 108 cache observations; both emission fixtures also check three
  same-address lifetime rejections. No in-game E1934 neutrality or restore run has
  been completed yet. Resource/manager checks still need targeted coverage.

## E1937–E1947 qualification update

E1937 capture-only and E1938 six restores with E1934 particles are neutral to
qualified SR06 player traces; E1939's death/two restores also match reference
continuations. The intentional death branch is retained but excluded from the
unchanged-gameplay comparison. This is not direct whole-particle visual parity.

E1940 exposed pending-deletion acceptance for typed shape/curve and impact
references (9/63 checks failed). E1941 adds the missing guards before any
particle mutation. E1942 passes the unchanged 63 resource checks and existing
90 history / 3 callback / 108 cache observations. E1946/E1947 pass 27 manager
lifetime checks using explicit retirement and same-address reconstruction in
the fixture; production does not acquire a manual manager replacement path.
E1943 capture-only with the exact E1941 candidate is neutral (24,622 complete
records per stream; 24,514 original-selected trajectory records). E1944 six
restores also match all 62,822 branch records per stream and collision checks.
Remaining runs are recorded in the main experiment log as they finish.

E1951's initial test premise incorrectly expected one update to unlink expired
particles; it actually marks lifespan exhausted, then unlinks on the next update.
The first fixture/result are retained. Corrected E1951b fails nine malformed-pool
checks on E1941 while valid-byte/emission checks pass. E1953 passes all58 pool
checks and the other five regressions on E1952. It covers three successive saved
states, repeated restore after real point-emitter expiration/reuse, every known
payload byte and subsequent emission order. This is not a MemorySanitizer run.

E1954–E1958 then pass capture-only SR06, six SR06 restores, two death restores,
four SR08 restores across play-state holds/ball transition and four SR10 restores.
Both player trace streams and collision comparisons pass; full visual particle
output and arbitrary third-party ownership are separate remaining gates.

Additional source audit: ParticleImpact's VxVector/Vx2DVector members are
constructed with zero defaults in this tree; deflector producers set the object
reference before enqueueing. They do not share the raw-allocated Particle pool's
free-slot construction gap. No speculative impact-format change was made.
