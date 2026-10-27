/******************************************************************************
 * Copyright (c) 2026 NVIDIA Corporation & Affiliates.                         *
 * All rights reserved.                                                        *
 *                                                                             *
 * This source code and the accompanying materials are made available under    *
 * the terms of the Apache License 2.0 which accompanies this distribution.  *
 ******************************************************************************/

#include "qlx/Dialect/QLX/Transforms/QLXSynthesize.h"
#include "qlx/Dialect/QLX/IR/QLXAttrs.h"
#include "qlx/Dialect/QLX/IR/QLXOps.h"
#include "qlx/Dialect/QLX/IR/QLXTypes.h"
#include "qlx/Dialect/QLX/Transforms/Passes.h"
#include "qlx/Dialect/QLX/Transforms/RotationSynthesizer.h"
#include "cudaq/Synthesis/Math/Real.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/IR/Builders.h"

#include <array>
#include <cfloat>
#include <cmath>
#include <complex>
#include <cstdint>
#include <map>
#include <optional>
#include <tuple>

namespace qlx {
#define GEN_PASS_DEF_QLXSYNTHESIZEROTATIONS
#define GEN_PASS_DEF_QLXVERIFYCLIFFORDT
#include "qlx/Dialect/QLX/Transforms/Passes.h.inc"
} // namespace qlx

using namespace mlir;
using namespace qlx;

namespace {

/// Basis changes in chronological application order. They map the selected
/// Pauli factor to Z before the parity rotation and undo the map afterwards.
/// S-dagger is normalized to S^3 so legalization emits positive generators.
static llvm::ArrayRef<BuiltinAction> prefixFor(char basis) {
  static const BuiltinAction kX[] = {BuiltinAction::h};
  static const BuiltinAction kY[] = {BuiltinAction::s, BuiltinAction::s,
                                     BuiltinAction::s, BuiltinAction::h};
  switch (basis) {
  case 'X':
    return kX;
  case 'Y':
    return kY;
  default:
    return {};
  }
}

static llvm::ArrayRef<BuiltinAction> suffixFor(char basis) {
  static const BuiltinAction kX[] = {BuiltinAction::h};
  static const BuiltinAction kY[] = {BuiltinAction::h, BuiltinAction::s};
  switch (basis) {
  case 'X':
    return kX;
  case 'Y':
    return kY;
  default:
    return {};
  }
}

/// Emit one built-in action and return the new linear qubit values.
static SmallVector<Value> emitApply(OpBuilder &builder, Location location,
                                    ValueRange inputs, BuiltinAction action) {
  SmallVector<Type> resultTypes;
  resultTypes.reserve(inputs.size());
  for (Value input : inputs)
    resultTypes.push_back(input.getType());
  auto actionAttr = BuiltinActionAttr::get(builder.getContext(), action);
  auto result =
      ApplyOp::create(builder, location, TypeRange(resultTypes), actionAttr,
                      inputs, /*parameters=*/DictionaryAttr{});
  return SmallVector<Value>(result.getResults());
}

static Value emitUnary(OpBuilder &builder, Location location, Value input,
                       BuiltinAction action) {
  return emitApply(builder, location, ValueRange{input}, action).front();
}

static Value emitSequence(OpBuilder &builder, Location location, Value input,
                          llvm::ArrayRef<BuiltinAction> actions) {
  Value current = input;
  for (BuiltinAction action : actions)
    current = emitUnary(builder, location, current, action);
  return current;
}

static void emitCx(OpBuilder &builder, Location location, Value &control,
                   Value &target) {
  SmallVector<Value, 2> inputs = {control, target};
  SmallVector<Value> results =
      emitApply(builder, location, inputs, BuiltinAction::cx);
  control = results[0];
  target = results[1];
}

static LogicalResult replaceResults(ApplyOp operation,
                                    ValueRange replacements) {
  if (operation.getNumResults() != replacements.size())
    return operation.emitOpError(
        "legalization produced the wrong number of linear results");
  for (auto [result, replacement] :
       llvm::zip_equal(operation.getResults(), replacements))
    result.replaceAllUsesWith(replacement);
  operation.erase();
  return success();
}

static LogicalResult replaceUnary(ApplyOp operation,
                                  llvm::ArrayRef<BuiltinAction> actions) {
  if (operation.getInputs().size() != 1 || operation.getNumResults() != 1)
    return operation.emitOpError(
        "expected a unary logical action during Clifford+T legalization");
  OpBuilder builder(operation);
  Value result = emitSequence(builder, operation.getLoc(),
                              operation.getInputs().front(), actions);
  return replaceResults(operation, ValueRange{result});
}

static SmallVector<Value, 3> emitCCZPhasePolynomial(OpBuilder &builder,
                                                    Location location,
                                                    ValueRange inputs) {
  static const BuiltinAction kTdg[] = {BuiltinAction::s, BuiltinAction::s,
                                       BuiltinAction::s, BuiltinAction::t};
  SmallVector<Value, 3> qubits(inputs);
  for (Value &qubit : qubits)
    qubit = emitUnary(builder, location, qubit, BuiltinAction::t);

  auto inverseTOnParity = [&](unsigned control, unsigned target) {
    emitCx(builder, location, qubits[control], qubits[target]);
    qubits[target] = emitSequence(builder, location, qubits[target], kTdg);
    emitCx(builder, location, qubits[control], qubits[target]);
  };
  inverseTOnParity(0, 1);
  inverseTOnParity(0, 2);
  inverseTOnParity(1, 2);

  emitCx(builder, location, qubits[0], qubits[2]);
  emitCx(builder, location, qubits[1], qubits[2]);
  qubits[2] = emitUnary(builder, location, qubits[2], BuiltinAction::t);
  emitCx(builder, location, qubits[1], qubits[2]);
  emitCx(builder, location, qubits[0], qubits[2]);
  return qubits;
}

/// Normalize the finite built-in logical actions to positive H/S/T/CX. CCZ is
/// realized by the seven-term phase polynomial
///
///   4abc = a+b+c-(a^b)-(a^c)-(b^c)+(a^b^c).
///
/// The deliberately simple compute/phase/uncompute network uses ten CX gates;
/// later circuit optimization may reduce it without changing this legalizer.
static LogicalResult legalizeFiniteActions(ModuleOp module) {
  SmallVector<ApplyOp> operations;
  module.walk([&](ApplyOp operation) { operations.push_back(operation); });

  static const BuiltinAction kSdg[] = {BuiltinAction::s, BuiltinAction::s,
                                       BuiltinAction::s};
  static const BuiltinAction kX[] = {BuiltinAction::h, BuiltinAction::s,
                                     BuiltinAction::s, BuiltinAction::h};
  static const BuiltinAction kY[] = {BuiltinAction::h, BuiltinAction::s,
                                     BuiltinAction::s, BuiltinAction::h,
                                     BuiltinAction::s, BuiltinAction::s};
  static const BuiltinAction kZ[] = {BuiltinAction::s, BuiltinAction::s};
  static const BuiltinAction kTdg[] = {BuiltinAction::s, BuiltinAction::s,
                                       BuiltinAction::s, BuiltinAction::t};

  for (ApplyOp operation : operations) {
    auto builtin = dyn_cast<BuiltinActionAttr>(operation.getActionAttr());
    if (!builtin)
      continue;
    switch (builtin.getValue()) {
    case BuiltinAction::h:
    case BuiltinAction::s:
    case BuiltinAction::t:
    case BuiltinAction::cx:
    case BuiltinAction::idle:
    case BuiltinAction::pauli_rotation:
    case BuiltinAction::u3:
      continue;
    case BuiltinAction::sdg:
      if (failed(replaceUnary(operation, kSdg)))
        return failure();
      continue;
    case BuiltinAction::x:
      if (failed(replaceUnary(operation, kX)))
        return failure();
      continue;
    case BuiltinAction::y:
      // X followed by Z is Y up to an irrelevant global phase.
      if (failed(replaceUnary(operation, kY)))
        return failure();
      continue;
    case BuiltinAction::z:
      if (failed(replaceUnary(operation, kZ)))
        return failure();
      continue;
    case BuiltinAction::tdg:
      if (failed(replaceUnary(operation, kTdg)))
        return failure();
      continue;
    case BuiltinAction::cz: {
      if (operation.getInputs().size() != 2 || operation.getNumResults() != 2)
        return operation.emitOpError(
            "expected a binary CZ during Clifford+T legalization");
      OpBuilder builder(operation);
      Value left = operation.getInputs()[0];
      Value right = emitUnary(builder, operation.getLoc(),
                              operation.getInputs()[1], BuiltinAction::h);
      emitCx(builder, operation.getLoc(), left, right);
      right = emitUnary(builder, operation.getLoc(), right, BuiltinAction::h);
      SmallVector<Value, 2> replacements = {left, right};
      if (failed(replaceResults(operation, replacements)))
        return failure();
      continue;
    }
    case BuiltinAction::ccz: {
      if (operation.getInputs().size() != 3 || operation.getNumResults() != 3)
        return operation.emitOpError(
            "expected a ternary CCZ during Clifford+T legalization");
      OpBuilder builder(operation);
      auto qubits = emitCCZPhasePolynomial(builder, operation.getLoc(),
                                           operation.getInputs());
      if (failed(replaceResults(operation, qubits)))
        return failure();
      continue;
    }
    case BuiltinAction::ccx: {
      if (operation.getInputs().size() != 3 || operation.getNumResults() != 3)
        return operation.emitOpError(
            "expected a ternary CCX during Clifford+T legalization");
      OpBuilder builder(operation);
      SmallVector<Value, 3> inputs(operation.getInputs());
      inputs[2] =
          emitUnary(builder, operation.getLoc(), inputs[2], BuiltinAction::h);
      auto qubits = emitCCZPhasePolynomial(builder, operation.getLoc(), inputs);
      qubits[2] =
          emitUnary(builder, operation.getLoc(), qubits[2], BuiltinAction::h);
      if (failed(replaceResults(operation, qubits)))
        return failure();
      continue;
    }
    }
  }
  return success();
}

/// Emit a positive-generator exact word for Rz(k*pi/4), with k reduced mod 8.
static void exactQuarterWord(int64_t quarterTurns,
                             llvm::SmallVectorImpl<BuiltinAction> &out) {
  int64_t reduced = ((quarterTurns % 8) + 8) % 8;
  for (int64_t i = 0; i < reduced / 2; ++i)
    out.push_back(BuiltinAction::s);
  if (reduced & 1)
    out.push_back(BuiltinAction::t);
}

/// Return the exact quarter-turn count when exact rational-pi source metadata
/// proves that the angle lies on the pi/4 Clifford+T lattice.
static std::optional<int64_t> exactQuarterTurns(ApplyOp operation) {
  auto parameters = operation.getParameters();
  if (!parameters)
    return std::nullopt;
  auto numerator =
      dyn_cast_or_null<IntegerAttr>(parameters->get("angle_pi_numer"));
  auto denominator =
      dyn_cast_or_null<IntegerAttr>(parameters->get("angle_pi_denom"));
  if (!numerator || !denominator || denominator.getInt() == 0)
    return std::nullopt;
  __int128 scaled = static_cast<__int128>(numerator.getInt()) * 4;
  __int128 denom = denominator.getInt();
  if (denom < 0) {
    scaled = -scaled;
    denom = -denom;
  }
  if (scaled % denom != 0)
    return std::nullopt;
  __int128 turns = scaled / denom;
  if (turns < INT64_MIN || turns > INT64_MAX)
    return std::nullopt;
  return static_cast<int64_t>(turns);
}

/// For float-authored angles, using the nearest exact lattice point is valid
/// only when that word itself meets the authored precision. This avoids the
/// old fixed-tolerance snap, which could violate tight precision requests.
static std::optional<int64_t> approximateQuarterTurns(double angle,
                                                      double precision) {
  if (!std::isfinite(angle) || !std::isfinite(precision) || precision <= 0)
    return std::nullopt;
  using cudaq::synth::Real;
  // Retain enough bits for cancellation even for very large f64 angles. The
  // nearest lattice index can exceed int64_t, so reduce it as a GMP integer.
  auto bits = static_cast<mpfr_prec_t>(
      64 + std::ceil(-std::log2(precision)) +
      (angle == 0 ? 0 : std::max(0, std::ilogb(std::abs(angle)) + 1)));
  cudaq::synth::ScopedDefaultPrecision workingPrecision(
      std::max<mpfr_prec_t>(128, bits));
  Real theta(angle), quarter = theta / (Real::pi() / Real(4));
  cudaq::synth::Integer nearest;
  mpfr_get_z(nearest.get_mpz_t(), quarter.get_mpfr(), MPFR_RNDN);

  // Bound the distance from the original f64 to n*pi/4 using directed
  // rounding. The projective Rz error is <= |theta - n*pi/4| / 2.
  Real lower, upper;
  mpfr_const_pi(lower.get_mpfr(), MPFR_RNDD);
  mpfr_const_pi(upper.get_mpfr(), MPFR_RNDU);
  if (mpz_sgn(nearest.get_mpz_t()) < 0)
    std::swap(lower, upper);
  mpfr_mul_z(lower.get_mpfr(), lower.get_mpfr(), nearest.get_mpz_t(),
             MPFR_RNDD);
  mpfr_mul_z(upper.get_mpfr(), upper.get_mpfr(), nearest.get_mpz_t(),
             MPFR_RNDU);
  mpfr_div_ui(lower.get_mpfr(), lower.get_mpfr(), 4, MPFR_RNDD);
  mpfr_div_ui(upper.get_mpfr(), upper.get_mpfr(), 4, MPFR_RNDU);
  mpfr_sub(lower.get_mpfr(), lower.get_mpfr(), theta.get_mpfr(), MPFR_RNDD);
  mpfr_sub(upper.get_mpfr(), upper.get_mpfr(), theta.get_mpfr(), MPFR_RNDU);
  mpfr_abs(lower.get_mpfr(), lower.get_mpfr(), MPFR_RNDU);
  mpfr_abs(upper.get_mpfr(), upper.get_mpfr(), MPFR_RNDU);
  Real budget = Real(precision) * Real(2);
  if (lower > budget || upper > budget)
    return std::nullopt;
  return static_cast<int64_t>(mpz_fdiv_ui(nearest.get_mpz_t(), 8));
}

/// Read the constant f64 angle feeding a pauli_rotation.
static FailureOr<double> constantAngle(Value angle) {
  if (auto constant = angle.getDefiningOp<arith::ConstantOp>())
    if (auto value = dyn_cast<FloatAttr>(constant.getValue()))
      return value.getValueAsDouble();
  return failure();
}

/// The validated +/-1 operator sign of a rotation.
static FailureOr<int64_t> rotationSign(ApplyOp operation) {
  auto parameters = operation.getParameters();
  auto attr = parameters
                  ? dyn_cast_or_null<IntegerAttr>(parameters->get("sign"))
                  : IntegerAttr{};
  int64_t sign = attr ? attr.getInt() : 1;
  if (sign != 1 && sign != -1)
    return operation.emitOpError("pauli rotation sign must be +1 or -1");
  return sign;
}

/// The sanctioned readers of a rotation's angle. Storage keeps the operator
/// sign in the canonical `sign` field with a nonnegative-magnitude angle
/// (uniform with mpp); these fold it back in so no caller re-derives the fold
/// and the sign can never be silently dropped (the original defect).
static FailureOr<double> signedAngle(ApplyOp operation) {
  auto magnitude = constantAngle(operation.getInputs().back());
  if (failed(magnitude))
    return failure();
  auto sign = rotationSign(operation);
  if (failed(sign))
    return failure();
  return static_cast<double>(*sign) * *magnitude;
}

static std::optional<int64_t> signedExactQuarterTurns(ApplyOp operation) {
  auto turns = exactQuarterTurns(operation);
  if (!turns)
    return std::nullopt;
  auto parameters = operation.getParameters();
  auto attr = parameters
                  ? dyn_cast_or_null<IntegerAttr>(parameters->get("sign"))
                  : IntegerAttr{};
  return (attr ? attr.getInt() : 1) * *turns;
}

/// Enforce the rotation normal form the sign accessors assume: the operator
/// sign is in the `sign` field and the angle is a nonnegative magnitude.
static LogicalResult verifyCanonicalRotation(ApplyOp operation, double angle) {
  if (angle < 0.0)
    return operation.emitOpError(
        "non-canonical rotation: the angle must be a nonnegative magnitude; "
        "the operator sign belongs in the sign field");
  if (auto parameters = operation.getParameters())
    if (auto numer =
            dyn_cast_or_null<IntegerAttr>(parameters->get("angle_pi_numer")))
      if (numer.getInt() < 0)
        return operation.emitOpError(
            "non-canonical rotation: angle_pi_numer must be nonnegative");
  return success();
}

using WordCache =
    std::map<std::tuple<char, double, double, double, double>, SynthesizedWord>;

static LogicalResult synthesizePauliRotations(ModuleOp module,
                                              double defaultPrecision,
                                              RotationSynthesizer &synthesizer,
                                              WordCache &cache) {
  SmallVector<ApplyOp> rotations;
  module.walk([&](ApplyOp operation) {
    auto builtin = dyn_cast<BuiltinActionAttr>(operation.getActionAttr());
    if (builtin && builtin.getValue() == BuiltinAction::pauli_rotation)
      rotations.push_back(operation);
  });

  for (ApplyOp operation : rotations) {
    if (operation.getInputs().empty())
      return operation.emitOpError("pauli rotation has no angle operand");
    auto angle = constantAngle(operation.getInputs().back());
    if (failed(angle))
      return operation.emitOpError(
          "Clifford+T synthesis requires a static angle; specialize the "
          "program parameter before synthesis");
    if (!std::isfinite(*angle))
      return operation.emitOpError("rotation angle must be finite");

    SmallVector<Value> qubits;
    for (Value input : operation.getInputs().drop_back())
      if (isa<LogicalQubitType>(input.getType()))
        qubits.push_back(input);
    if (qubits.empty() || qubits.size() > 63 ||
        operation.getNumResults() != qubits.size())
      return operation.emitOpError(
          "Clifford+T synthesis requires 1..63 logical Pauli operands");

    auto parameters = operation.getParameters();
    auto xAttr = parameters
                     ? dyn_cast_or_null<IntegerAttr>(parameters->get("x_mask"))
                     : IntegerAttr{};
    auto zAttr = parameters
                     ? dyn_cast_or_null<IntegerAttr>(parameters->get("z_mask"))
                     : IntegerAttr{};
    if (!xAttr || !zAttr)
      return operation.emitOpError(
          "pauli rotation is missing its x_mask/z_mask parameters");

    double precision = defaultPrecision;
    if (parameters)
      if (auto authored =
              dyn_cast_or_null<FloatAttr>(parameters->get("precision")))
        precision = authored.getValueAsDouble();
    if (!std::isfinite(precision) || precision <= 0.0 || precision >= 1.0)
      return operation.emitOpError(
          "rotation precision must be finite and in the open interval (0, 1)");

    if (failed(verifyCanonicalRotation(operation, *angle)))
      return failure();
    auto signedTheta = signedAngle(operation);
    if (failed(signedTheta))
      return failure();

    uint64_t xMask = static_cast<uint64_t>(xAttr.getInt());
    uint64_t zMask = static_cast<uint64_t>(zAttr.getInt());
    uint64_t validMask = qubits.size() == 64
                             ? ~uint64_t{0}
                             : ((uint64_t{1} << qubits.size()) - 1);
    if (((xMask | zMask) & ~validMask) != 0)
      return operation.emitOpError(
          "pauli masks reference operands outside the rotation");

    SmallVector<char> bases;
    bases.reserve(qubits.size());
    for (unsigned index = 0; index < qubits.size(); ++index) {
      bool x = xMask & (uint64_t{1} << index);
      bool z = zMask & (uint64_t{1} << index);
      if (!x && !z)
        return operation.emitOpError(
            "identity factors are not legal rotation operands");
      bases.push_back(x && z ? 'Y' : (x ? 'X' : 'Z'));
    }

    SmallVector<BuiltinAction> core;
    std::optional<int64_t> exactTurns = signedExactQuarterTurns(operation);
    if (exactTurns) {
      exactQuarterWord(*exactTurns, core);
    } else if (auto turns = approximateQuarterTurns(*signedTheta, precision)) {
      exactQuarterWord(*turns, core);
    } else {
      auto key = std::make_tuple('z', *signedTheta, 0.0, 0.0, precision);
      auto found = cache.find(key);
      if (found == cache.end()) {
        auto word = synthesizer.synthesizeRz(*signedTheta, precision);
        if (failed(word))
          return operation.emitOpError("rotation synthesizer '")
                 << synthesizer.name()
                 << "' failed: " << synthesizer.lastError();
        found = cache.emplace(key, std::move(*word)).first;
      }
      core = found->second.actions;
    }

    OpBuilder builder(operation);
    for (unsigned index = 0; index < qubits.size(); ++index)
      qubits[index] = emitSequence(builder, operation.getLoc(), qubits[index],
                                   prefixFor(bases[index]));

    unsigned accumulator = qubits.size() - 1;
    for (unsigned index = 0; index < accumulator; ++index)
      emitCx(builder, operation.getLoc(), qubits[index], qubits[accumulator]);
    qubits[accumulator] =
        emitSequence(builder, operation.getLoc(), qubits[accumulator], core);
    for (unsigned index = accumulator; index-- > 0;)
      emitCx(builder, operation.getLoc(), qubits[index], qubits[accumulator]);

    for (unsigned index = 0; index < qubits.size(); ++index)
      qubits[index] = emitSequence(builder, operation.getLoc(), qubits[index],
                                   suffixFor(bases[index]));

    if (failed(replaceResults(operation, qubits)))
      return failure();
  }
  return success();
}

static LogicalResult replaceUnaryU3(ApplyOp operation,
                                    ArrayRef<BuiltinAction> actions) {
  OpBuilder builder(operation);
  Value result = emitSequence(builder, operation.getLoc(),
                              operation.getInputs().front(), actions);
  return replaceResults(operation, ValueRange{result});
}

/// Find the shortest positive H/S word for one of the 24 projective Cliffords.
/// Signed Pauli images keep this exact: no numerical snapping is involved.
static SmallVector<BuiltinAction>
exactU3Clifford(const std::array<int64_t, 3> &turns) {
  using Frame = std::array<int, 3>;
  const Frame identity = {1, 2, 3};
  const Frame h = {3, -2, 1}, s = {2, -1, 3}, ry = {-3, 2, 1};
  auto compose = [](Frame frame, const Frame &gate) {
    for (int &image : frame)
      image = (image < 0 ? -1 : 1) * gate[std::abs(image) - 1];
    return frame;
  };
  Frame target = identity;
  for (unsigned index : {2u, 0u, 1u})
    for (int64_t count = 0; count < (turns[index] % 8) / 2; ++count)
      target = compose(target, index == 0 ? ry : s);
  struct Entry {
    Frame frame;
    SmallVector<BuiltinAction> word;
  };
  SmallVector<Entry> queue = {{identity, {}}};
  std::map<Frame, bool> seen = {{identity, true}};
  for (unsigned index = 0; index < queue.size(); ++index) {
    Entry entry = queue[index];
    if (entry.frame == target)
      return entry.word;
    for (BuiltinAction gate : {BuiltinAction::h, BuiltinAction::s}) {
      Frame next = compose(entry.frame, gate == BuiltinAction::h ? h : s);
      if (!seen.emplace(next, true).second)
        continue;
      auto word = entry.word;
      word.push_back(gate);
      queue.push_back({next, std::move(word)});
    }
  }
  llvm_unreachable("single-qubit Clifford group is generated by H and S");
}

static LogicalResult synthesizeU3Actions(ModuleOp module,
                                         RotationSynthesizer &synthesizer,
                                         double defaultPrecision,
                                         WordCache &cache) {
  SmallVector<ApplyOp> operations;
  module.walk([&](ApplyOp operation) {
    auto builtin = dyn_cast<BuiltinActionAttr>(operation.getActionAttr());
    if (builtin && builtin.getValue() == BuiltinAction::u3)
      operations.push_back(operation);
  });
  for (ApplyOp operation : operations) {
    auto parameters = operation.getParameters().value_or(DictionaryAttr{});
    double precision = defaultPrecision;
    if (parameters)
      if (auto attr = parameters.get("precision")) {
        auto value = dyn_cast<FloatAttr>(attr);
        if (!value || !value.getType().isF64())
          return operation.emitOpError("u3 precision must be an f64 attribute");
        precision = value.getValueAsDouble();
      }
    if (!std::isfinite(precision) || precision <= 0 || precision >= 1)
      return operation.emitOpError(
          "rotation precision must be finite and in the open interval (0, 1)");
    std::array<double, 3> angles;
    std::array<std::optional<int64_t>, 3> turns;
    bool hasExactLatticeMetadata = false;
    const char *names[] = {"theta", "phi", "lambda"};
    for (unsigned index = 0; index < 3; ++index) {
      auto angle = constantAngle(operation.getInputs()[index + 1]);
      if (failed(angle))
        return operation.emitOpError(
            "Clifford+T synthesis requires a static angle; specialize the "
            "program parameter before synthesis");
      if (!std::isfinite(*angle))
        return operation.emitOpError("u3 angles must be finite");
      angles[index] = *angle;
      std::string prefix = std::string(names[index]) + "_pi_";
      Attribute numerator =
          parameters ? parameters.get(prefix + "numer") : Attribute{};
      Attribute denominator =
          parameters ? parameters.get(prefix + "denom") : Attribute{};
      if (numerator || denominator) {
        auto numer = dyn_cast_or_null<IntegerAttr>(numerator);
        auto denom = dyn_cast_or_null<IntegerAttr>(denominator);
        if (!numer || !denom || !numer.getType().isInteger(64) ||
            !denom.getType().isInteger(64) || denom.getInt() <= 0 ||
            numer.getInt() < 0 ||
            static_cast<__int128>(numer.getInt()) >=
                2 * static_cast<__int128>(denom.getInt()))
          return operation.emitOpError(
              "u3 requires canonical rational-pi metadata");
        double expected = M_PI * (static_cast<double>(numer.getInt()) /
                                  static_cast<double>(denom.getInt()));
        if (std::abs(expected - angles[index]) > 8e-15)
          return operation.emitOpError(
              "u3 angle disagrees with rational-pi metadata");
        __int128 scaled = static_cast<__int128>(numer.getInt()) * 4;
        if (scaled % denom.getInt() == 0) {
          turns[index] = static_cast<int64_t>(scaled / denom.getInt());
          hasExactLatticeMetadata = true;
        }
      } else {
        turns[index] = approximateQuarterTurns(angles[index], precision / 3);
      }
    }
    bool allExact =
        llvm::all_of(turns, [](auto value) { return value.has_value(); });
    if (allExact &&
        llvm::all_of(turns, [](auto value) { return *value % 2 == 0; })) {
      auto word = exactU3Clifford({*turns[0], *turns[1], *turns[2]});
      if (failed(replaceUnaryU3(operation, word)))
        return failure();
      continue;
    }
    // Whole-U3 callbacks accept f64 values. Keep authoritative exact lattice
    // metadata on the split path, even when another Euler angle is off lattice.
    if (synthesizer.supportsU3() && !allExact && !hasExactLatticeMetadata) {
      auto key =
          std::make_tuple('u', angles[0], angles[1], angles[2], precision);
      auto found = cache.find(key);
      if (found == cache.end()) {
        auto word = synthesizer.synthesizeU3(angles[0], angles[1], angles[2],
                                             precision);
        if (failed(word))
          return operation.emitOpError("rotation synthesizer '")
                 << synthesizer.name()
                 << "' failed: " << synthesizer.lastError();
        found = cache.emplace(key, std::move(*word)).first;
      }
      if (failed(replaceUnaryU3(operation, found->second.actions)))
        return failure();
      continue;
    }
    OpBuilder builder(operation);
    Value qubit = operation.getInputs().front();
    for (unsigned index : {2u, 0u, 1u}) {
      NamedAttrList attrs;
      attrs.set("x_mask", builder.getI64IntegerAttr(index == 0 ? 1 : 0));
      attrs.set("z_mask", builder.getI64IntegerAttr(1));
      attrs.set("sign", builder.getI64IntegerAttr(angles[index] < 0 ? -1 : 1));
      attrs.set("precision", builder.getF64FloatAttr(precision / 3));
      std::string prefix = std::string(names[index]) + "_pi_";
      if (parameters && parameters.get(prefix + "numer")) {
        attrs.set("angle_pi_numer", parameters.get(prefix + "numer"));
        attrs.set("angle_pi_denom", parameters.get(prefix + "denom"));
      }
      Value angle = arith::ConstantOp::create(
          builder, operation.getLoc(),
          builder.getF64FloatAttr(std::abs(angles[index])));
      auto rotation = ApplyOp::create(
          builder, operation.getLoc(), TypeRange{qubit.getType()},
          BuiltinActionAttr::get(builder.getContext(),
                                 BuiltinAction::pauli_rotation),
          ValueRange{qubit, angle}, builder.getDictionaryAttr(attrs));
      qubit = rotation.getResult(0);
    }
    if (failed(replaceResults(operation, ValueRange{qubit})))
      return failure();
  }
  return success();
}

using Mat2 = std::array<std::complex<double>, 4>;

/// Row-major 2x2 product `left * right`.
static Mat2 multiply(const Mat2 &left, const Mat2 &right) {
  return Mat2{left[0] * right[0] + left[1] * right[2],
              left[0] * right[1] + left[1] * right[3],
              left[2] * right[0] + left[3] * right[2],
              left[2] * right[1] + left[3] * right[3]};
}

/// exp(-i angle P / 2) for the Pauli `basis` in {X, Y, Z}.
static Mat2 pauliRotationUnitary(char basis, double angle) {
  const std::complex<double> i(0.0, 1.0);
  double c = std::cos(angle / 2), s = std::sin(angle / 2);
  switch (basis) {
  case 'X':
    return Mat2{c, -i * s, -i * s, c};
  case 'Y':
    return Mat2{c, -s, s, c};
  default:
    return Mat2{std::exp(-i * (angle / 2)), 0.0, 0.0,
                std::exp(i * (angle / 2))};
  }
}

/// The u3 convention of this pass: Rz(phi) Ry(theta) Rz(lambda).
static Mat2 u3Unitary(double theta, double phi, double lambda) {
  return multiply(pauliRotationUnitary('Z', phi),
                  multiply(pauliRotationUnitary('Y', theta),
                           pauliRotationUnitary('Z', lambda)));
}

static std::optional<Mat2> cliffordTUnitary(BuiltinAction action) {
  switch (action) {
  case BuiltinAction::h:
    return Mat2{M_SQRT1_2, M_SQRT1_2, M_SQRT1_2, -M_SQRT1_2};
  case BuiltinAction::s:
    return Mat2{1.0, 0.0, 0.0, std::complex<double>(0.0, 1.0)};
  case BuiltinAction::t:
    return Mat2{1.0, 0.0, 0.0, std::polar(1.0, M_PI / 4)};
  default:
    return std::nullopt;
  }
}

/// Inverse of u3Unitary, up to global phase.
static std::array<double, 3> zyzAngles(const Mat2 &unitary) {
  std::complex<double> root =
      std::sqrt(unitary[0] * unitary[3] - unitary[1] * unitary[2]);
  std::complex<double> diagonal = unitary[0] / root, lower = unitary[2] / root;
  double theta = 2.0 * std::atan2(std::abs(lower), std::abs(diagonal));
  double sum = -2.0 * std::arg(diagonal), difference = 2.0 * std::arg(lower);
  return {theta, (sum + difference) / 2.0, (sum - difference) / 2.0};
}

struct RunMember {
  Mat2 unitary;
  std::optional<double> precision;
  bool offLattice = false;
};

/// Anything unrecognized (dynamic, malformed, or exact rational-pi intent)
/// ends a run and is left to the existing legalization.
static std::optional<RunMember> runMember(ApplyOp operation,
                                          double defaultPrecision) {
  auto builtin = dyn_cast<BuiltinActionAttr>(operation.getActionAttr());
  if (!builtin || operation.getNumResults() != 1 ||
      operation.getInputs().empty())
    return std::nullopt;
  if (auto unitary = cliffordTUnitary(builtin.getValue()))
    return RunMember{*unitary, std::nullopt, false};

  auto parameters = operation.getParameters().value_or(DictionaryAttr{});
  auto integer = [&](StringRef name) {
    return parameters ? dyn_cast_or_null<IntegerAttr>(parameters.get(name))
                      : IntegerAttr{};
  };
  double precision = defaultPrecision;
  if (parameters)
    if (Attribute attr = parameters.get("precision")) {
      auto authored = dyn_cast<FloatAttr>(attr);
      if (!authored || !authored.getType().isF64())
        return std::nullopt;
      precision = authored.getValueAsDouble();
    }
  if (!std::isfinite(precision) || precision <= 0.0 || precision >= 1.0)
    return std::nullopt;

  switch (builtin.getValue()) {
  case BuiltinAction::pauli_rotation: {
    auto xMask = integer("x_mask"), zMask = integer("z_mask");
    auto signAttr = integer("sign");
    int64_t sign = signAttr ? signAttr.getInt() : 1;
    auto magnitude = constantAngle(operation.getInputs().back());
    if (operation.getInputs().size() != 2 || !xMask || !zMask ||
        xMask.getInt() < 0 || xMask.getInt() > 1 || zMask.getInt() < 0 ||
        zMask.getInt() > 1 || xMask.getInt() + zMask.getInt() == 0 ||
        (sign != 1 && sign != -1) || failed(magnitude) ||
        !std::isfinite(*magnitude) || *magnitude < 0.0)
      return std::nullopt;
    bool exactLattice = signedExactQuarterTurns(operation).has_value();
    if (!exactLattice && integer("angle_pi_numer"))
      return std::nullopt;
    double angle = static_cast<double>(sign) * *magnitude;
    char basis = xMask.getInt() ? (zMask.getInt() ? 'Y' : 'X') : 'Z';
    bool onLattice =
        exactLattice || approximateQuarterTurns(angle, precision).has_value();
    return RunMember{pauliRotationUnitary(basis, angle), precision, !onLattice};
  }
  case BuiltinAction::u3: {
    if (operation.getInputs().size() != 4)
      return std::nullopt;
    std::array<double, 3> angles;
    bool onLattice = true;
    const char *names[] = {"theta", "phi", "lambda"};
    for (unsigned index = 0; index < 3; ++index) {
      std::string prefix = std::string(names[index]) + "_pi_";
      if (parameters && (parameters.get(prefix + "numer") ||
                         parameters.get(prefix + "denom")))
        return std::nullopt;
      auto angle = constantAngle(operation.getInputs()[index + 1]);
      if (failed(angle) || !std::isfinite(*angle))
        return std::nullopt;
      angles[index] = *angle;
      onLattice = onLattice &&
                  approximateQuarterTurns(*angle, precision / 3).has_value();
    }
    return RunMember{u3Unitary(angles[0], angles[1], angles[2]), precision,
                     !onLattice};
  }
  default:
    return std::nullopt;
  }
}

/// Fuse each single-qubit chain containing an off-lattice rotation into one u3
/// at the tightest member precision, less f64 rounding slack.
static void mergeSingleQubitRuns(ModuleOp module, double defaultPrecision) {
  llvm::MapVector<Operation *, RunMember> members;
  module.walk([&](ApplyOp operation) {
    if (auto member = runMember(operation, defaultPrecision))
      members.insert({operation.getOperation(), std::move(*member)});
  });
  auto successor = [&](ApplyOp operation) -> ApplyOp {
    Value result = operation.getResult(0);
    if (!result.hasOneUse())
      return {};
    auto user = dyn_cast<ApplyOp>(*result.getUsers().begin());
    if (!user || user->getBlock() != operation->getBlock() ||
        !members.count(user.getOperation()) ||
        user.getInputs().front() != result)
      return {};
    return user;
  };

  SmallVector<SmallVector<ApplyOp>> runs;
  for (auto &entry : members) {
    auto operation = cast<ApplyOp>(entry.first);
    auto producer = operation.getInputs().front().getDefiningOp<ApplyOp>();
    if (producer && members.count(producer.getOperation()) &&
        successor(producer) == operation)
      continue;
    SmallVector<ApplyOp> run{operation};
    while (ApplyOp next = successor(run.back()))
      run.push_back(next);
    if (run.size() > 1)
      runs.push_back(std::move(run));
  }

  for (auto &run : runs) {
    Mat2 product{1.0, 0.0, 0.0, 1.0};
    double precision = 1.0;
    bool offLattice = false;
    for (ApplyOp operation : run) {
      const RunMember &member = members.find(operation.getOperation())->second;
      product = multiply(member.unitary, product);
      if (member.precision)
        precision = std::min(precision, *member.precision);
      offLattice = offLattice || member.offLattice;
    }
    double slack = 64.0 * run.size() * DBL_EPSILON;
    if (!offLattice || precision < 1.0e4 * run.size() * DBL_EPSILON)
      continue;

    std::array<double, 3> angles = zyzAngles(product);
    ApplyOp last = run.back();
    OpBuilder builder(last);
    SmallVector<Value, 4> inputs{run.front().getInputs().front()};
    for (double angle : angles)
      inputs.push_back(arith::ConstantOp::create(builder, last.getLoc(),
                                                 builder.getF64FloatAttr(angle))
                           .getResult());
    NamedAttrList attrs;
    attrs.set("precision", builder.getF64FloatAttr(precision - slack));
    auto merged = ApplyOp::create(
        builder, last.getLoc(), TypeRange{inputs.front().getType()},
        BuiltinActionAttr::get(builder.getContext(), BuiltinAction::u3),
        ValueRange(inputs), builder.getDictionaryAttr(attrs));
    last.getResult(0).replaceAllUsesWith(merged.getResult(0));
    for (ApplyOp operation : llvm::reverse(run))
      operation.erase();
  }
}

} // namespace

LogicalResult qlx::verifyCliffordT(ModuleOp module) {
  bool failedVerification = false;
  module.walk([&](ApplyOp operation) {
    auto builtin = dyn_cast<BuiltinActionAttr>(operation.getActionAttr());
    if (!builtin) {
      operation.emitOpError(
          "is not a built-in action legal in the Clifford+T gate set");
      failedVerification = true;
      return;
    }
    switch (builtin.getValue()) {
    case BuiltinAction::h:
    case BuiltinAction::s:
    case BuiltinAction::t:
    case BuiltinAction::cx:
    case BuiltinAction::idle:
      return;
    default:
      operation.emitOpError(
          "remains outside the Clifford+T gate set after legalization");
      failedVerification = true;
      return;
    }
  });
  return failure(failedVerification);
}

LogicalResult qlx::synthesizeRotations(ModuleOp module, double precision,
                                       StringRef synthesizerName,
                                       bool mergeSingleQubit) {
  if (!std::isfinite(precision) || precision <= 0.0 || precision >= 1.0)
    return module.emitError(
        "default synthesis precision must be finite and in (0, 1)");
  auto synthesizer = lookupRotationSynthesizer(synthesizerName);
  if (!synthesizer)
    return module.emitError("unknown rotation synthesizer '")
           << synthesizerName << "'";
  WordCache cache;
  MLIRContext *context = module.getContext();
  context->getOrLoadDialect<arith::ArithDialect>();

  if (failed(legalizeFiniteActions(module)))
    return failure();
  if (mergeSingleQubit && synthesizer->supportsU3())
    mergeSingleQubitRuns(module, precision);
  if (failed(synthesizeU3Actions(module, *synthesizer, precision, cache)))
    return failure();
  if (failed(synthesizePauliRotations(module, precision, *synthesizer, cache)))
    return failure();
  return success();
}

namespace {

struct QLXSynthesizeRotationsPass
    : public qlx::impl::QLXSynthesizeRotationsBase<QLXSynthesizeRotationsPass> {
  using QLXSynthesizeRotationsBase::QLXSynthesizeRotationsBase;
  void runOnOperation() override {
    if (failed(qlx::synthesizeRotations(getOperation(), precision, synthesizer,
                                        mergeSingleQubit)))
      signalPassFailure();
  }
};

struct QLXVerifyCliffordTPass
    : public qlx::impl::QLXVerifyCliffordTBase<QLXVerifyCliffordTPass> {
  void runOnOperation() override {
    if (failed(qlx::verifyCliffordT(getOperation())))
      signalPassFailure();
  }
};

} // namespace
