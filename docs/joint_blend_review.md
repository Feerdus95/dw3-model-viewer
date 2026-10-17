# Joint blending: per-pair review

Each pair was inspected with `dw3mv --inspect A,B --only A,B --shot out.bmp file.bin` (rigid left, blended right) at the audited worst pose.
A good ratio or a low residual separation never decided anything: a verdict is what the sheet showed. Decisions live in
`src/modules/blend_decisions.inc` (explicit, apart from eligibility and from the geometric verdict).

Matrix: **approved** (clear improvement, drawn by default), **excluded** (destructive or a hinge: never drawn, `--weld-unreviewed` and `--weld` included),
**pending** (evidence insufficient, neutral, ambiguous or unresolved: drawn only by `--weld`, never by `--weld-unreviewed`).
Always rejected, as confirmed destructive and not part of this table: M122HAGU 2<-1 (`BLEND_REJECT`). Always excluded as a rigid hinge:
Guilmon 7<-6 and Brachiomon 17<-16 (`BLEND_SKIP`).

State is the geometric state (validated, review = ratio under 85% of the rigid ring in its pose, rejected). Ratio: blended ring over the rigid ring of the same pose.
Separation: the matched vertices, rigid -> blended, units. Disp: the largest displacement of any vertex, units.

| model | pair | state | pose | ratio | separation | disp | verdict | decision | reason |
|---|---|---|---|---|---|---|---|---|---|
| 0356 M366GIRU | b4<-b3 | validated | m3 k16 | 91% | 124.0 -> 1.0 | 62 | improved | **pending** | seen from 4 angles at m3 k16 and in 3 more poses: the lump is the forearm bending smoothly into the glove where the rigid view has a step; nothing crushed. Awaiting approval by a person |
| 0502 M126BRAK | b9<-b8 | validated | m12 k8 | 96% | 324.0 -> 0.0 | 162 | improved | **pending** | the TAIL (bones 8-10) crack closes with this pair alone, at m12 k8 and k28 (10<-9 alone leaves it). Awaiting approval by a person |
| 0502 M126BRAK | b10<-b9 | validated | m12 k28 | 96% | 216.0 -> 1.0 | 108 | neutral | **pending** | alone it does not close the tail crack at m12 k8 and k28; no visible effect elsewhere |
| 1381 M077REDV | b12<-b11 | validated | m6 k8 | 92% | 90.0 -> 0.0 | 45 | improved | **approved** | the step between the two segments is gone: one smooth limb |
| 0736 M025VEGI | b13<-b12 | validated | m6 k8 | 92% | 90.0 -> 1.1 | 45 | improved | **approved** | the step between the two segments is gone: one smooth limb |
| 1766 M378BVND | b3<-b2 | validated | m51 k176 | 89% | 1600.0 -> 1.4 | 801 | degraded | **excluded** | a jaw hinge: blended, the open mouth closes (it validates by the numbers; the metrics cannot tell a hinge) |
| 1766 M378BVND | b11<-b10 | validated | m51 k176 | 89% | 1600.0 -> 1.4 | 801 | degraded | **excluded** | a jaw hinge: same model, same behaviour as 3<-2 |
| 1798 M241HNGY | b19<-b18 | review | m3 k16 | 77% | 278.0 -> 1.0 | 139 | degraded | **pending** | the boot pinches and creases at the red band (kept disabled until resolved) |
| 1798 M241HNGY | b23<-b22 | review | m2 k60 | 78% | 275.0 -> 1.0 | 138 | degraded | **pending** | the boot pinches and creases at the red band (kept disabled until resolved) |
| 1798 M241HNGY | b3<-b2 | review | m13 k80 | 66% | 301.0 -> 1.0 | 150 | degraded | **pending** | the red band shrinks and the blue piece pinches |
| 1798 M241HNGY | b6<-b5 | review | m13 k80 | 66% | 301.0 -> 1.0 | 150 | degraded | **pending** | the red band shrinks to a sliver |
| 0600 M041GIRO | b3<-b2 | review | m15 k40 | 55% | 180.0 -> 0.0 | 90 | degraded | **pending** | the narrow tube pinches at the ring (ratio 55%): ring and tube both thin |
| 0737 M108GOLE | b3<-b2 | review | m13 k104 | 50% | 466.0 -> 1.4 | 234 | degraded | **pending** | a sharp spike grows at the junction of the arm pieces |
| 0620 M110GECO | b7<-b6 | review | m15 k80 | 54% | 98.0 -> 1.4 | 49 | degraded | **pending** | the upper piece kinks where the rigid view shows a gap |
| 1335 M143CLOK | b9<-b8 | review | m13 k44 | 51% | 237.0 -> 0.0 | 119 | neutral | **pending** | no visible crack or defect either way |
| 1335 M143CLOK | b11<-b10 | review | m13 k44 | 51% | 237.0 -> 0.0 | 119 | neutral | **pending** | no visible crack or defect either way |
| 0620 M110GECO | b10<-b9 | review | m13 k44 | 54% | 98.0 -> 1.4 | 49 | neutral | **pending** | no visible difference |
| 0511 M373RENA | b17<-b16 | review | m3 k12 | 57% | 83.0 -> 1.4 | 41 | neutral | **pending** | no visible difference |
| 1832 M389KYKI | b14<-b13 | review | m28 k116 | 63% | 86.0 -> 1.2 | 44 | inconclusive | **pending** | collapsed to a line at the audited pose; at motions 5 and 10 it has thickness and shows no difference |
| 0509 M236VVND | b9<-b8 | validated | m13 k52 | 88% | 241.0 -> 71.0 | 134 | neutral | **pending** | a wing joint kept 71 units apart at rest; blending leaves the gap as it was |
| 0682 M059HOUO | b19<-b18 | validated | m3 k16 | 96% | 131.0 -> 28.4 | 79 | neutral | **pending** | no visible difference |
| 1848 M466RGN2 | b7<-b6 | validated | m1 k0 | 100% | 50.0 -> 49.5 | 0 | inconclusive | **pending** | no relative motion in any pose (displacement 0): nothing to compare |
| 1848 M466RGN2 | b6<-b2 | validated | m1 k0 | 100% | 56.0 -> 55.6 | 0 | inconclusive | **pending** | no relative motion in any pose (displacement 0): nothing to compare |
| 1104 M214SRPH | b13<-b1 | review | m51 k92 | 84% | 399.0 -> 0.0 | 200 | neutral | **pending** | no visible difference |
| 1615 M247DEMN | b9<-b8 | review | m39 k48 | 76% | 434.0 -> 1.4 | 218 | neutral | **pending** | no visible difference |
| 1615 M247DEMN | b3<-b2 | review | m13 k84 | 75% | 355.0 -> 1.4 | 178 | neutral | **pending** | slight narrowing only |
| 0612 M023YUKI | b8<-b7 | review | m13 k44 | 72% | 562.0 -> 1.0 | 281 | inconclusive | **pending** | the white block pair folds differently; neither view is clearly right |
| 1056 M383PKEN | b14<-b13 | review | m14 k48 | 79% | 173.0 -> 1.4 | 87 | neutral | **pending** | a smoother bend, no defect fixed or made |
| 1056 M383PKEN | b19<-b18 | review | m14 k48 | 79% | 183.0 -> 1.0 | 92 | neutral | **pending** | a smoother bend, no defect fixed or made |
| 1056 M383PKEN | b9<-b8 | review | m16 k28 | 82% | 212.0 -> 1.4 | 106 | neutral | **pending** | a changed but not clearly better shape |
| 0619 M052MOJA | b3<-b2 | review | m13 k44 | 80% | 468.0 -> 1.4 | 234 | inconclusive | **pending** | an inner shard grows into a point; unclear whether it is right |
| 0619 M052MOJA | b7<-b6 | review | m13 k44 | 82% | 447.0 -> 1.4 | 223 | improved | **pending** | the triangular hole in the rigid view closes, but only one pose was inspected |
| 1821 M465SNAT | b21<-b20 | review | m1 k20 | 78% | 116.0 -> 1.4 | 59 | neutral | **pending** | no visible difference |
| 0581 M384KOET | b9<-b8 | review | m14 k20 | 77% | 103.0 -> 1.4 | 51 | neutral | **pending** | no visible difference |
| 0581 M384KOET | b3<-b2 | review | m14 k20 | 81% | 94.0 -> 1.4 | 47 | neutral | **pending** | no visible difference |
| 0621 M004BETA | b9<-b8 | review | m12 k28 | 75% | 136.0 -> 4.6 | 69 | neutral | **pending** | no visible difference |
| 0952 M224MDEV | b18<-b17 | review | m2 k100 | 67% | 366.0 -> 0.0 | 183 | neutral | **pending** | a small kink in a thin arm; rigid is bent too |
| 0952 M224MDEV | b21<-b20 | review | m2 k100 | 67% | 368.0 -> 0.0 | 184 | neutral | **pending** | no visible difference |
| 0591 M031PATA | b20<-b19 | review | m19 k20 | 69% | 152.0 -> 1.6 | 76 | inconclusive | **pending** | the dark gap in the rigid view closes but the white shape changes; one pose only |
| 0591 M031PATA | b23<-b22 | review | m19 k20 | 83% | 152.0 -> 1.6 | 76 | inconclusive | **pending** | the dark gap in the rigid view closes but the white shape changes; one pose only |
| 0356 M366GIRU | b22<-b13 | review | m6 k12 | 81% | 250.0 -> 1.4 | 125 | neutral | **pending** | almost no difference |
| 1263 M273QTRU | b6<-b5 | review | m6 k52 | 82% | 182.0 -> 1.0 | 91 | neutral | **pending** | no visible difference |
| 1382 M269VALK | b22<-b10 | review | m17 k24 | 83% | 442.0 -> 2.8 | 222 | neutral | **pending** | no visible difference |
| 1740 M334MLDR | b16<-b15 | review | m11 k108 | 83% | 1089.0 -> 1.4 | 545 | neutral | **pending** | no visible difference (the ring shades slightly differently) |
| 0736 M025VEGI | b11<-b10 | review | m11 k44 | 84% | 118.0 -> 0.0 | 59 | neutral | **pending** | no visible difference |
| 1381 M077REDV | b10<-b9 | review | m11 k44 | 84% | 118.0 -> 0.0 | 59 | neutral | **pending** | no visible difference |
| 1148 M066VAND | b3<-b2 | validated | m12 k24 | 99% | 294.0 -> 17.3 | 148 | inconclusive | **pending** | collapsed to a line at the audited pose; at motion 1 key 0 it shows no difference |
| 1112 M375TAOM | b13<-b12 | validated | m18 k8 | 95% | 182.0 -> 6.3 | 91 | neutral | **pending** | shows no difference at motion 1 key 0 |

## Reproducing the five approved pairs

From `viewer/`, with `DATA=..\native\data\files`. Sheets: rigid left, blended right, same pose and camera. Drop `--only A,B` for the surrounding model.

    dw3mv --inspect 3,4  --only 3,4  --motion 3  --key 16 --shot wrist_ring.bmp   %DATA%\0356.bin   (Guilmon wrist; also --motion 17 --key 42, the README's reference pose)
    dw3mv --inspect 8,9  --only 8,9  --motion 12 --key 8 --shot tail98.bmp       %DATA%\0502.bin   (Brachiomon tail 9<-8)
    dw3mv --inspect 9,10 --only 9,10 --motion 12 --key 28 --shot tail109.bmp      %DATA%\0502.bin   (Brachiomon tail 10<-9)
    dw3mv --inspect 11,12 --only 11,12 --motion 6 --key 8  --shot redv_ring.bmp   %DATA%\1381.bin   (REDV 12<-11; extreme: --motion 11 --key 24)
    dw3mv --inspect 12,13 --only 12,13 --motion 6 --key 8  --shot vegi_ring.bmp   %DATA%\0736.bin   (VEGI 13<-12; extreme: --motion 11 --key 24)

The motion and key are the audited worst-ring pose; the "disp" pose (largest displacement) is the same for the first three pairs and
motion 11 key 24 for REDV and VEGI. `python tools/test_blend_policy.py` checks the activation policy (what is and is not drawn).

## Second look (after the first five sheets)

Approved now: REDV 12<-11 and VEGI 13<-12 only. Brachiomon 9<-8 and Guilmon 4<-3 are *pending* with an `improved` verdict (a person has to approve them);
Brachiomon 10<-9 is pending, `neutral`. Bones 8-10 of Brachiomon are its **tail**, not its neck.

Per-pair tail check (the crack closes with 9<-8 alone, not with 10<-9 alone), motion 12 key 8 and key 28:

    dw3mv --shot a.bmp --motion 12 --key 8 --only 8,9,10 --zoom 1.6 %DATA%\0502.bin                                    (rigid)
    dw3mv --shot b.bmp --motion 12 --key 8 --only 8,9,10 --zoom 1.6 --blend --weld 8,9 %DATA%\0502.bin                 (9<-8 only)
    dw3mv --shot c.bmp --motion 12 --key 8 --only 8,9,10 --zoom 1.6 --blend --weld 9,10 %DATA%\0502.bin                (10<-9 only)

Guilmon's wrist, from four angles: `dw3mv --inspect 3,4 --only 3,4 --yaw 34|124|214|304 --zoom 1.8 --motion 3 --key 16 --shot w.bmp %DATA%\0356.bin`.
