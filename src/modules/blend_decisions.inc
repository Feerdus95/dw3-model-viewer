/* The per-pair decisions: which joints were looked at (--inspect, at the audited worst pose, rigid beside blended), and what was decided.
   Kept apart from eligibility and from the geometric verdict, and explicit and deterministic: nothing here is a rule or a threshold.
     APPROVED  a clear visual improvement, seen in more than one pose, and no relevant deformation: drawn by default (when the geometry still validates).
     EXCLUDED  a destructive deformation or a joint that must stay rigid (a hinge): never drawn, not even by --weld-unreviewed or --weld.
     PENDING   not enough evidence, an ambiguous or unjudgeable pose, or a defect not resolved: drawn only when asked for by name (--weld),
               never by --weld-unreviewed. "Neutral" (no change worth having) is pending too.
   A pair nobody has looked at has no entry; --weld-unreviewed still draws it if it validates (an experiment, not a decision).
   The kind is the ACTIVATION state; the verdict is what the sheets showed, and only at the poses inspected (MOTION, KEY): a kind is not evidence.
   Bones are the file's numbers; MOTION and KEY are the pose that was inspected. The figures (ratio, separation, displacement) are
   what `dw3mv --blend-audit` prints for the pair; docs/joint_blend_review.md has them next to each decision. */
enum BlendDecisionKind { BLEND_APPROVED, BLEND_EXCLUDED, BLEND_PENDING };
struct BlendDecision {
    const char *model;
    int a, b;
    BlendDecisionKind kind;
    const char *verdict; /* improved, neutral, degraded, mixed (good in some poses, bad in others), inconclusive */
    int motion, key;
    const char *why;
};
static const BlendDecision BLEND_DECISIONS[] = {
    {"M366GIRU.BIN", 3, 4, BLEND_APPROVED, "improved", 3, 16, "seen from 4 angles at motion 3 key 16 and in 3 other poses: the lump is the forearm bending smoothly into the glove where the rigid view has a step; nothing crushed. Approved after review of the sheets."},
    {"M126BRAK.BIN", 8, 9, BLEND_APPROVED, "improved", 12, 8, "the TAIL (bones 8-10) crack closes with this pair alone, at motion 12 keys 8 and 28 (10<-9 alone leaves it). Approved after review of the sheets."},
    {"M126BRAK.BIN", 9, 10, BLEND_PENDING, "neutral", 12, 28, "alone it does not close the tail crack at motion 12 keys 8 and 28; no visible effect elsewhere"},
    {"M077REDV.BIN", 11, 12, BLEND_APPROVED, "improved", 6, 8, "the step between the two segments is gone: one smooth limb"},
    {"M025VEGI.BIN", 12, 13, BLEND_APPROVED, "improved", 6, 8, "the step between the two segments is gone: one smooth limb"},
    {"M378BVND.BIN", 2, 3, BLEND_EXCLUDED, "degraded", 51, 176, "a jaw hinge: blended, the open mouth closes (it validates by the numbers; the metrics cannot tell a hinge)"},
    {"M378BVND.BIN", 10, 11, BLEND_EXCLUDED, "degraded", 51, 176, "a jaw hinge: same model, same behaviour as 3<-2"},
    {"M241HNGY.BIN", 18, 19, BLEND_PENDING, "degraded", 3, 16, "a pointed flap is pulled out of the blue boot in every pose seen (motion 3 key 16, idle, motion 5 key 20): a constant defect, not an extreme-pose one"},
    {"M241HNGY.BIN", 22, 23, BLEND_PENDING, "neutral", 2, 60, "near-identical at idle and motion 5 key 20; at the audited pose (motion 2 key 60) the lower piece is pulled up under the upper one with a crease"},
    {"M241HNGY.BIN", 2, 3, BLEND_PENDING, "improved", 13, 80, "With --weld-fade, 8 poses (motions 2-15): the gap at the red band closes in about 5 of 8 and nothing degrades. Needs the fade on; awaiting approval"},
    {"M241HNGY.BIN", 5, 6, BLEND_PENDING, "neutral", 13, 80, "With --weld-fade, 8 poses (motions 2-15): almost identical to rigid, a slightly smoother band at times; no defect"},
    {"M041GIRO.BIN", 2, 3, BLEND_PENDING, "improved", 15, 40, "With --weld-fade, 8 poses (motions 2-15): the torn white ring becomes one clean band and the elbow is natural in all 8; no pinch. Needs the fade on; awaiting approval"},
    {"M108GOLE.BIN", 2, 3, BLEND_PENDING, "neutral", 13, 104, "With --weld-fade, 8 poses (motions 2-15): identical to rigid in all 8 (the fade cuts 87% of its poses): nothing gained"},
    {"M110GECO.BIN", 6, 7, BLEND_PENDING, "improved", 15, 80, "With --weld-fade, 8 poses (motions 2-15): a smoother elbow, no wedge; the gain is small. Needs the fade on; awaiting approval"},
    {"M143CLOK.BIN", 8, 9, BLEND_PENDING, "neutral", 13, 44, "no visible crack or defect either way"},
    {"M143CLOK.BIN", 10, 11, BLEND_PENDING, "neutral", 13, 44, "no visible crack or defect either way"},
    {"M110GECO.BIN", 9, 10, BLEND_PENDING, "neutral", 13, 44, "no visible difference"},
    {"M373RENA.BIN", 16, 17, BLEND_PENDING, "neutral", 3, 12, "no visible difference"},
    {"M389KYKI.BIN", 13, 14, BLEND_PENDING, "inconclusive", 28, 116, "collapsed to a line at the audited pose; at motions 5 and 10 it has thickness and shows no difference"},
    {"M236VVND.BIN", 8, 9, BLEND_PENDING, "neutral", 13, 52, "a wing joint kept 71 units apart at rest; blending leaves the gap as it was"},
    {"M059HOUO.BIN", 18, 19, BLEND_PENDING, "neutral", 3, 16, "no visible difference"},
    {"M466RGN2.BIN", 6, 7, BLEND_PENDING, "inconclusive", 1, 0, "no relative motion in any pose (displacement 0): nothing to compare"},
    {"M466RGN2.BIN", 2, 6, BLEND_PENDING, "inconclusive", 1, 0, "no relative motion in any pose (displacement 0): nothing to compare"},
    {"M214SRPH.BIN", 1, 13, BLEND_PENDING, "neutral", 51, 92, "no visible difference"},
    {"M247DEMN.BIN", 8, 9, BLEND_PENDING, "neutral", 39, 48, "no visible difference"},
    {"M247DEMN.BIN", 2, 3, BLEND_PENDING, "neutral", 13, 84, "slight narrowing only"},
    {"M023YUKI.BIN", 7, 8, BLEND_PENDING, "inconclusive", 13, 44, "the white block pair folds differently; neither view is clearly right"},
    {"M383PKEN.BIN", 13, 14, BLEND_PENDING, "neutral", 14, 48, "a smoother bend, no defect fixed or made"},
    {"M383PKEN.BIN", 18, 19, BLEND_PENDING, "neutral", 14, 48, "a smoother bend, no defect fixed or made"},
    {"M383PKEN.BIN", 8, 9, BLEND_PENDING, "neutral", 16, 28, "a changed but not clearly better shape"},
    {"M052MOJA.BIN", 2, 3, BLEND_PENDING, "inconclusive", 13, 44, "an inner shard grows into a point; unclear whether it is right"},
    {"M052MOJA.BIN", 6, 7, BLEND_PENDING, "improved", 13, 44, "the triangular hole in the rigid view closes, but only one pose was inspected"},
    {"M465SNAT.BIN", 20, 21, BLEND_PENDING, "neutral", 1, 20, "no visible difference"},
    {"M384KOET.BIN", 8, 9, BLEND_PENDING, "neutral", 14, 20, "no visible difference"},
    {"M384KOET.BIN", 2, 3, BLEND_PENDING, "neutral", 14, 20, "no visible difference"},
    {"M004BETA.BIN", 8, 9, BLEND_PENDING, "neutral", 12, 28, "no visible difference"},
    {"M224MDEV.BIN", 17, 18, BLEND_PENDING, "neutral", 2, 100, "a small kink in a thin arm; rigid is bent too"},
    {"M224MDEV.BIN", 20, 21, BLEND_PENDING, "neutral", 2, 100, "no visible difference"},
    {"M031PATA.BIN", 19, 20, BLEND_PENDING, "inconclusive", 19, 20, "the dark gap in the rigid view closes but the white shape changes; one pose only"},
    {"M031PATA.BIN", 22, 23, BLEND_PENDING, "inconclusive", 19, 20, "the dark gap in the rigid view closes but the white shape changes; one pose only"},
    {"M366GIRU.BIN", 13, 22, BLEND_PENDING, "neutral", 6, 12, "almost no difference"},
    {"M273QTRU.BIN", 5, 6, BLEND_PENDING, "neutral", 6, 52, "no visible difference"},
    {"M269VALK.BIN", 10, 22, BLEND_PENDING, "neutral", 17, 24, "no visible difference"},
    {"M334MLDR.BIN", 15, 16, BLEND_PENDING, "neutral", 11, 108, "no visible difference (the ring shades slightly differently)"},
    {"M025VEGI.BIN", 10, 11, BLEND_PENDING, "neutral", 11, 44, "no visible difference"},
    {"M077REDV.BIN", 9, 10, BLEND_PENDING, "neutral", 11, 44, "no visible difference"},
    {"M066VAND.BIN", 2, 3, BLEND_PENDING, "inconclusive", 12, 24, "collapsed to a line at the audited pose; at motion 1 key 0 it shows no difference"},
    {"M375TAOM.BIN", 12, 13, BLEND_PENDING, "neutral", 18, 8, "shows no difference at motion 1 key 0"},
};
