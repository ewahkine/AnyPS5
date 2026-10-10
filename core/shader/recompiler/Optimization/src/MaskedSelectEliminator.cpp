#include "Optimization/MaskedSelectEliminator.hpp"
#include "Optimization/DeadCodeEliminator.hpp"
#include "RdnaDecoder/RdnaInstruction.hpp"
#include <algorithm>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ShaderRecompiler {
namespace {

constexpr std::size_t VisitLimit = 65536;
constexpr std::uint32_t DepthLimit = 8;
constexpr std::uint32_t ImplicationDepthLimit = 64;
constexpr std::size_t ImplicationStepLimit = 16384;

bool isSelect(IrOpcode opcode) {
    return opcode == IrOpcode::SelectU1 || opcode == IrOpcode::SelectU32 || opcode == IrOpcode::SelectF32;
}

bool isLaneLocal(IrOpcode opcode) {
    if (IrOpcodeHasSideEffects(opcode)) return false;
    switch (opcode) {
        case IrOpcode::Identity:
        case IrOpcode::IAdd32:
        case IrOpcode::ISub32:
        case IrOpcode::IMul32:
        case IrOpcode::SelectU1:
        case IrOpcode::SelectU32:
        case IrOpcode::SelectF32:
        case IrOpcode::BitCastU16F16:
        case IrOpcode::BitCastF16U16:
        case IrOpcode::BitCastU32F32:
        case IrOpcode::BitCastF32U32:
        case IrOpcode::ConvertU16U32:
        case IrOpcode::ConvertU32U16:
        case IrOpcode::ConvertU8U32:
        case IrOpcode::ConvertU32U8:
        case IrOpcode::ConvertF32F16:
        case IrOpcode::ConvertF16F32:
        case IrOpcode::ConvertS32F32:
        case IrOpcode::ConvertU32F32:
        case IrOpcode::ConvertF32S32:
        case IrOpcode::ConvertF32U32:
        case IrOpcode::ConvertF32F64:
        case IrOpcode::ConvertF64F32:
        case IrOpcode::ConvertF64S32:
        case IrOpcode::ConvertF64U32:
        case IrOpcode::ConvertS32F64:
        case IrOpcode::ConvertU32F64:
        case IrOpcode::CompositeConstructU64:
        case IrOpcode::CompositeConstructU32x2:
        case IrOpcode::CompositeConstructU32x3:
        case IrOpcode::CompositeConstructF32x2:
        case IrOpcode::CompositeConstructU32x4:
        case IrOpcode::CompositeExtractU64:
        case IrOpcode::CompositeExtractU32x2:
        case IrOpcode::CompositeExtractU32x3:
        case IrOpcode::CompositeExtractU32x4:
        case IrOpcode::PackHalf2x16:
        case IrOpcode::PackFloat2x16Rtz:
        case IrOpcode::BitFieldInsert:
        case IrOpcode::BitFieldUExtract:
        case IrOpcode::BitFieldSExtract:
        case IrOpcode::IAdd64:
        case IrOpcode::IAddCarry32:
        case IrOpcode::ISub64:
        case IrOpcode::IMul64:
        case IrOpcode::SMulHi:
        case IrOpcode::UMulHi:
        case IrOpcode::IAbs32:
        case IrOpcode::ShiftLeftLogical32:
        case IrOpcode::ShiftLeftLogical64:
        case IrOpcode::ShiftRightLogical32:
        case IrOpcode::ShiftRightLogical64:
        case IrOpcode::ShiftRightArithmetic32:
        case IrOpcode::ShiftRightArithmetic64:
        case IrOpcode::BitwiseAnd32:
        case IrOpcode::BitwiseAnd64:
        case IrOpcode::BitwiseOr32:
        case IrOpcode::BitwiseXor32:
        case IrOpcode::BitwiseNot32:
        case IrOpcode::BitReverse32:
        case IrOpcode::BitCount32:
        case IrOpcode::BitCount64:
        case IrOpcode::FindUMsb32:
        case IrOpcode::FindUMsb64:
        case IrOpcode::FindILsb32:
        case IrOpcode::SMin32:
        case IrOpcode::UMin32:
        case IrOpcode::SMax32:
        case IrOpcode::UMax32:
        case IrOpcode::SMinTri32:
        case IrOpcode::UMinTri32:
        case IrOpcode::SMaxTri32:
        case IrOpcode::UMaxTri32:
        case IrOpcode::SMedTri32:
        case IrOpcode::UMedTri32:
        case IrOpcode::SLessThan32:
        case IrOpcode::SLessThan64:
        case IrOpcode::ULessThan32:
        case IrOpcode::ULessThan64:
        case IrOpcode::IEqual32:
        case IrOpcode::IEqual64:
        case IrOpcode::SLessThanEqual32:
        case IrOpcode::ULessThanEqual32:
        case IrOpcode::SGreaterThan32:
        case IrOpcode::UGreaterThan32:
        case IrOpcode::UGreaterThan64:
        case IrOpcode::INotEqual32:
        case IrOpcode::INotEqual64:
        case IrOpcode::SGreaterThanEqual32:
        case IrOpcode::UGreaterThanEqual32:
        case IrOpcode::LogicalOr:
        case IrOpcode::LogicalAnd:
        case IrOpcode::LogicalXor:
        case IrOpcode::LogicalNot:
        case IrOpcode::FPOrdEqual32:
        case IrOpcode::FPUnordEqual32:
        case IrOpcode::FPOrdNotEqual32:
        case IrOpcode::FPUnordNotEqual32:
        case IrOpcode::FPOrdLessThan32:
        case IrOpcode::FPUnordLessThan32:
        case IrOpcode::FPOrdGreaterThan32:
        case IrOpcode::FPUnordGreaterThan32:
        case IrOpcode::FPOrdLessThanEqual32:
        case IrOpcode::FPUnordLessThanEqual32:
        case IrOpcode::FPOrdGreaterThanEqual32:
        case IrOpcode::FPUnordGreaterThanEqual32:
        case IrOpcode::FPIsNan32:
        case IrOpcode::FPCmpClass32:
        case IrOpcode::FPAbs32:
        case IrOpcode::FPNeg32:
        case IrOpcode::FPSaturate32:
        case IrOpcode::FPAdd32:
        case IrOpcode::FPSub32:
        case IrOpcode::FPFma32:
        case IrOpcode::FPMad32:
        case IrOpcode::FPMul32:
        case IrOpcode::FPMin32:
        case IrOpcode::FPMax32:
        case IrOpcode::FPMinTri32:
        case IrOpcode::FPMaxTri32:
        case IrOpcode::FPMedTri32:
        case IrOpcode::FPRecip32:
        case IrOpcode::FPRecipIFlag32:
        case IrOpcode::FPRecipSqrt32:
        case IrOpcode::FPSqrt:
        case IrOpcode::FPSin:
        case IrOpcode::FPCos:
        case IrOpcode::FPExp2:
        case IrOpcode::FPLog2:
        case IrOpcode::FPRoundEven32:
        case IrOpcode::FPFloor32:
        case IrOpcode::FPCeil32:
        case IrOpcode::FPTrunc32:
        case IrOpcode::FPFract32:
        case IrOpcode::FPAdd64:
        case IrOpcode::FPMul64:
        case IrOpcode::FPFma64:
        case IrOpcode::FPMin64:
        case IrOpcode::FPMax64:
        case IrOpcode::FPSaturate64:
        case IrOpcode::FPLdexp64:
        case IrOpcode::FPRoundEven64:
        case IrOpcode::FPFloor64:
        case IrOpcode::FPCeil64:
        case IrOpcode::FPTrunc64:
        case IrOpcode::FPFract64:
        case IrOpcode::FPFrexpMant64:
        case IrOpcode::FPFrexpExp64:
        case IrOpcode::MakeImageAddress:
            return true;
        default:
            return false;
    }
}

bool readsOnlyWhereActive(IrOpcode opcode) {
    switch (opcode) {
        case IrOpcode::LoadBufferU8:
        case IrOpcode::LoadBufferU16:
        case IrOpcode::LoadBufferU32:
        case IrOpcode::LoadBufferU32x2:
        case IrOpcode::LoadBufferU32x3:
        case IrOpcode::LoadBufferU32x4:
        case IrOpcode::StoreBufferU8:
        case IrOpcode::StoreBufferU16:
        case IrOpcode::StoreBufferU32:
        case IrOpcode::StoreBufferU32x2:
        case IrOpcode::StoreBufferU32x3:
        case IrOpcode::StoreBufferU32x4:
        case IrOpcode::LoadSharedU8:
        case IrOpcode::LoadSharedU16:
        case IrOpcode::LoadSharedU32:
        case IrOpcode::LoadSharedU32x2:
        case IrOpcode::LoadSharedU32x3:
        case IrOpcode::LoadSharedU32x4:
        case IrOpcode::WriteSharedU8:
        case IrOpcode::WriteSharedU16:
        case IrOpcode::WriteSharedU32:
        case IrOpcode::WriteSharedU32x2:
        case IrOpcode::WriteSharedU32x3:
        case IrOpcode::WriteSharedU32x4:
        case IrOpcode::SetAttribute:
            return true;
        default:
            return false;
    }
}

bool isExplicitLodSample(const IrProgram& program, const IrValue& value) {
    if (value.Opcode() != IrOpcode::ImageSampleRaw) return false;
    if (program.Resources().stage != IrShaderStage::Pixel) return true;
    const auto index = value.Flags<MemoryFlags>().index;
    if (index >= program.Resources().memoryInfo.size()) return false;
    return (program.Resources().memoryInfo[index].imageSampleFlags & (RdnaImageSampleFlagDerivative | RdnaImageSampleFlagLod | RdnaImageSampleFlagLevelZero)) != 0u;
}

bool isImmediate(const IrValue* value, std::uint32_t immediate) {
    return value->HasImmediate() && value->Type() == IrType::U32 && value->ImmediateU32() == immediate;
}

const IrValue* otherArgument(const IrValue* value, std::uint32_t immediate) {
    if (value->ArgumentCount() != 2u) return nullptr;
    const IrValue* left = value->Argument(0)->Resolve();
    const IrValue* right = value->Argument(1)->Resolve();
    if (isImmediate(right, immediate)) return left;
    if (isImmediate(left, immediate)) return right;
    return nullptr;
}

struct ThreadBit {
    const IrValue* low = nullptr;
    const IrValue* high = nullptr;
};

bool isLane(const IrValue* value) {
    return value->Resolve()->Opcode() == IrOpcode::LaneId;
}

std::optional<ThreadBit> threadBit(const IrValue* value, std::uint32_t waveSize) {
    value = value->Resolve();
    if (value->Opcode() != IrOpcode::INotEqual32) return std::nullopt;
    const IrValue* bit = otherArgument(value, 0u);
    if (bit == nullptr || bit->Opcode() != IrOpcode::BitwiseAnd32) return std::nullopt;
    const IrValue* shifted = otherArgument(bit, 1u);
    if (shifted == nullptr || shifted->Opcode() != IrOpcode::ShiftRightLogical32) return std::nullopt;
    const IrValue* amount = shifted->Argument(1)->Resolve();
    if (amount->Opcode() != IrOpcode::BitwiseAnd32) return std::nullopt;
    const IrValue* lane = otherArgument(amount, 31u);
    if (lane == nullptr || !isLane(lane)) return std::nullopt;
    const IrValue* word = shifted->Argument(0)->Resolve();
    if (waveSize == 32u || word->HasImmediate()) return ThreadBit{word, word};
    if (word->Opcode() != IrOpcode::SelectU32) return std::nullopt;
    const IrValue* half = word->Argument(0)->Resolve();
    if (half->Opcode() != IrOpcode::ULessThan32 || !isLane(half->Argument(0)) || !isImmediate(half->Argument(1)->Resolve(), 32u)) return std::nullopt;
    return ThreadBit{word->Argument(1)->Resolve(), word->Argument(2)->Resolve()};
}

bool alwaysTrue(const IrValue* value, std::uint32_t waveSize, std::uint32_t depth = 0) {
    value = value->Resolve();
    if (value->HasImmediate()) return value->Type() == IrType::Bool && value->ImmediateBool();
    if (depth > DepthLimit) return false;
    if (value->Opcode() == IrOpcode::LogicalAnd) return alwaysTrue(value->Argument(0), waveSize, depth + 1u) && alwaysTrue(value->Argument(1), waveSize, depth + 1u);
    const auto bit = threadBit(value, waveSize);
    return bit && isImmediate(bit->low, 0xffffffffu) && isImmediate(bit->high, 0xffffffffu);
}

struct Implication {
    const IrValue* mask;
    std::uint32_t waveSize;
    std::vector<std::pair<const IrValue*, const IrValue*>> assumedWords;
    std::vector<const IrValue*> assumedConditions;
    std::size_t steps = 0;
};

bool implies(const IrValue* condition, Implication& proof, std::uint32_t depth);
bool wordsImply(const IrValue* low, const IrValue* high, Implication& proof, std::uint32_t depth);

bool exhausted(Implication& proof, std::uint32_t depth) {
    return depth > ImplicationDepthLimit || ++proof.steps > ImplicationStepLimit;
}

bool phiConditionImplies(const IrValue* condition, Implication& proof, std::uint32_t depth) {
    if (std::ranges::find(proof.assumedConditions, condition) != proof.assumedConditions.end()) return true;
    const auto open = proof.assumedConditions.size();
    proof.assumedConditions.push_back(condition);
    bool result = true;
    for (std::size_t index = 0; result && index < condition->ArgumentCount(); ++index) {
        result = implies(condition->Argument(index), proof, depth + 1u);
    }
    proof.assumedConditions.resize(open);
    return result;
}

bool phiWordsImply(const IrValue* low, const IrValue* high, Implication& proof, std::uint32_t depth) {
    const bool wide = proof.waveSize != 32u;
    if (wide && (!high->IsPhi() || high->Parent() != low->Parent())) return false;
    const std::pair<const IrValue*, const IrValue*> words{low, wide ? high : nullptr};
    if (std::ranges::find(proof.assumedWords, words) != proof.assumedWords.end()) return true;
    const auto open = proof.assumedWords.size();
    proof.assumedWords.push_back(words);
    bool result = true;
    for (std::size_t index = 0; result && index < low->PhiBlockCount(); ++index) {
        const IrValue* highWord = high;
        if (wide) {
            highWord = nullptr;
            for (std::size_t highIndex = 0; highIndex < high->PhiBlockCount(); ++highIndex) {
                if (high->PhiBlock(highIndex) == low->PhiBlock(index)) highWord = high->Argument(highIndex)->Resolve();
            }
        }
        result = (!wide || highWord != nullptr) && wordsImply(low->Argument(index)->Resolve(), highWord, proof, depth + 1u);
    }
    proof.assumedWords.resize(open);
    return result;
}

bool wordsImply(const IrValue* low, const IrValue* high, Implication& proof, std::uint32_t depth) {
    const auto waveSize = proof.waveSize;
    if (isImmediate(low, 0u) && (waveSize == 32u || isImmediate(high, 0u))) return true;
    if (exhausted(proof, depth)) return false;
    if (low->Opcode() == IrOpcode::CompositeExtractU32x4 && isImmediate(low->Argument(1)->Resolve(), 0u)) {
        const IrValue* ballot = low->Argument(0)->Resolve();
        const bool sameBallot = waveSize == 32u || (high->Opcode() == IrOpcode::CompositeExtractU32x4 && high->Argument(0)->Resolve() == ballot && isImmediate(high->Argument(1)->Resolve(), 1u));
        if (ballot->Opcode() == IrOpcode::Ballot && sameBallot && implies(ballot->Argument(0), proof, depth + 1u)) return true;
    }
    if (low->IsPhi()) return phiWordsImply(low, high, proof, depth);
    if (low->Opcode() != IrOpcode::BitwiseAnd32) return false;
    if (waveSize == 32u) return wordsImply(low->Argument(0)->Resolve(), high, proof, depth + 1u) || wordsImply(low->Argument(1)->Resolve(), high, proof, depth + 1u);
    if (high->Opcode() != IrOpcode::BitwiseAnd32) return false;
    for (std::size_t lowIndex = 0; lowIndex < 2u; ++lowIndex) {
        for (std::size_t highIndex = 0; highIndex < 2u; ++highIndex) {
            if (wordsImply(low->Argument(lowIndex)->Resolve(), high->Argument(highIndex)->Resolve(), proof, depth + 1u)) return true;
        }
    }
    return false;
}

bool implies(const IrValue* condition, Implication& proof, std::uint32_t depth) {
    condition = condition->Resolve();
    if (condition == proof.mask || alwaysTrue(proof.mask, proof.waveSize)) return true;
    if (condition->HasImmediate()) return condition->Type() == IrType::Bool && !condition->ImmediateBool();
    if (exhausted(proof, depth)) return false;
    if (condition->Opcode() == IrOpcode::LogicalAnd) return implies(condition->Argument(0), proof, depth + 1u) || implies(condition->Argument(1), proof, depth + 1u);
    if (condition->IsPhi()) return phiConditionImplies(condition, proof, depth);
    const auto bit = threadBit(condition, proof.waveSize);
    return bit && wordsImply(bit->low, bit->high, proof, depth + 1u);
}

bool implies(const IrValue* condition, const IrValue* mask, std::uint32_t waveSize) {
    Implication proof{mask, waveSize};
    return implies(condition, proof, 0u);
}

std::unordered_set<const IrBlock*> blocksOutsideLoops(const IrProgram& program) {
    const auto& order = program.BlockOrder();
    if (order.empty()) return {};
    std::unordered_map<const IrBlock*, int> state;
    std::unordered_set<const IrBlock*> looping;
    std::vector<std::pair<const IrBlock*, std::size_t>> stack;
    state[order.front()] = 1;
    stack.emplace_back(order.front(), 0u);
    while (!stack.empty()) {
        auto& [block, next] = stack.back();
        if (next == block->Successors().size()) {
            state[block] = 2;
            stack.pop_back();
            continue;
        }
        const IrBlock* source = block;
        const IrBlock* successor = block->Successors()[next++];
        int& successorState = state[successor];
        if (successorState == 0) {
            successorState = 1;
            stack.emplace_back(successor, 0u);
        } else if (successorState == 1) {
            std::unordered_set<const IrBlock*> body{successor};
            std::vector<const IrBlock*> pending{source};
            while (!pending.empty()) {
                const IrBlock* member = pending.back();
                pending.pop_back();
                if (!body.insert(member).second) continue;
                for (const IrBlock* predecessor : member->Predecessors()) pending.push_back(predecessor);
            }
            looping.insert(body.begin(), body.end());
        }
    }
    std::unordered_set<const IrBlock*> result;
    for (const auto& [block, visited] : state) {
        if (visited == 2 && !looping.contains(block)) result.insert(block);
    }
    return result;
}

bool evaluatedOnce(const std::unordered_set<const IrBlock*>& once, const IrValue* value) {
    return value->HasImmediate() || (value->Parent() != nullptr && once.contains(value->Parent()));
}

bool excludesOnlyHelpers(const IrProgram& program, const IrValue* mask) {
    if (program.Resources().stage != IrShaderStage::Pixel || mask->Opcode() != IrOpcode::IEqual32) return false;
    const IrValue* builtin = otherArgument(mask, 0u);
    return builtin != nullptr && builtin->Opcode() == IrOpcode::GetBuiltin &&
           isImmediate(builtin->Argument(0)->Resolve(), static_cast<std::uint32_t>(StageInputKind::HelperInvocation)) && isImmediate(builtin->Argument(1)->Resolve(), 0u);
}

bool discardedForHelpers(IrOpcode opcode) {
    switch (opcode) {
        case IrOpcode::StoreBufferU8:
        case IrOpcode::StoreBufferU16:
        case IrOpcode::StoreBufferU32:
        case IrOpcode::StoreBufferU32x2:
        case IrOpcode::StoreBufferU32x3:
        case IrOpcode::StoreBufferU32x4:
        case IrOpcode::SetAttribute:
            return true;
        default:
            return false;
    }
}

bool clearWhereMasked(const IrValue* word, std::uint32_t half, const IrValue* mask, std::uint32_t waveSize, std::vector<const IrValue*>& assumed, std::uint32_t depth = 0) {
    word = word->Resolve();
    if (isImmediate(word, 0u)) return true;
    if (depth > DepthLimit) return false;
    if (word->Opcode() == IrOpcode::CompositeExtractU32x4) {
        const IrValue* ballot = word->Argument(0)->Resolve();
        return ballot->Opcode() == IrOpcode::Ballot && isImmediate(word->Argument(1)->Resolve(), half) && implies(ballot->Argument(0), mask, waveSize);
    }
    if (word->Opcode() == IrOpcode::BitwiseAnd32) {
        return clearWhereMasked(word->Argument(0), half, mask, waveSize, assumed, depth + 1u) || clearWhereMasked(word->Argument(1), half, mask, waveSize, assumed, depth + 1u);
    }
    if (word->Opcode() == IrOpcode::BitwiseOr32) {
        return clearWhereMasked(word->Argument(0), half, mask, waveSize, assumed, depth + 1u) && clearWhereMasked(word->Argument(1), half, mask, waveSize, assumed, depth + 1u);
    }
    if (!word->IsPhi()) return false;
    if (std::ranges::find(assumed, word) != assumed.end()) return true;
    const auto open = assumed.size();
    assumed.push_back(word);
    bool result = true;
    for (std::size_t index = 0; result && index < word->ArgumentCount(); ++index) {
        result = clearWhereMasked(word->Argument(index), half, mask, waveSize, assumed, depth + 1u);
    }
    assumed.resize(open);
    return result;
}

bool clearWhereMasked(const IrValue* word, std::uint32_t half, const IrValue* mask, std::uint32_t waveSize) {
    std::vector<const IrValue*> assumed;
    return clearWhereMasked(word, half, mask, waveSize, assumed);
}

bool isLaneShift(const IrValue* amount) {
    amount = amount->Resolve();
    if (amount->Opcode() != IrOpcode::BitwiseAnd32) return false;
    const IrValue* lane = otherArgument(amount, 31u);
    return lane != nullptr && isLane(lane);
}

bool isHalfSelect(const IrValue* condition) {
    condition = condition->Resolve();
    return condition->Opcode() == IrOpcode::ULessThan32 && isLane(condition->Argument(0)) && isImmediate(condition->Argument(1)->Resolve(), 32u);
}

enum class Taint {
    Lane,
    Ballot,
    LowWord,
    HighWord,
    LaneWord,
    LaneBits,
};

std::optional<Taint> taintOfUse(const IrProgram& program, const IrUse& use, Taint taint) {
    const auto waveSize = program.WaveSize();
    const IrValue* user = use.user;
    const auto opcode = user->Opcode();
    const auto other = [&] { return user->Argument(1u - use.operand)->Resolve(); };
    switch (taint) {
        case Taint::Lane:
            if (opcode == IrOpcode::Ballot) return Taint::Ballot;
            if (user->IsPhi() || isLaneLocal(opcode) || isExplicitLodSample(program, *user)) return Taint::Lane;
            return std::nullopt;
        case Taint::Ballot: {
            if (opcode != IrOpcode::CompositeExtractU32x4 || use.operand != 0u) return std::nullopt;
            const IrValue* component = user->Argument(1)->Resolve();
            if (isImmediate(component, 0u)) return Taint::LowWord;
            if (waveSize != 32u && isImmediate(component, 1u)) return Taint::HighWord;
            return std::nullopt;
        }
        case Taint::LowWord:
        case Taint::HighWord:
            if (user->IsPhi() || opcode == IrOpcode::Identity || opcode == IrOpcode::BitwiseAnd32 || opcode == IrOpcode::BitwiseOr32 || opcode == IrOpcode::BitwiseXor32 || opcode == IrOpcode::BitwiseNot32) return taint;
            if (opcode == IrOpcode::SelectU32 && use.operand != 0u && waveSize != 32u && isHalfSelect(user->Argument(0)) && use.operand == (taint == Taint::LowWord ? 1u : 2u)) return Taint::LaneWord;
            if (opcode == IrOpcode::ShiftRightLogical32 && use.operand == 0u && waveSize == 32u && isLaneShift(user->Argument(1))) return Taint::LaneBits;
            return std::nullopt;
        case Taint::LaneWord:
            if (opcode == IrOpcode::ShiftRightLogical32 && use.operand == 0u && isLaneShift(user->Argument(1))) return Taint::LaneBits;
            return std::nullopt;
        case Taint::LaneBits:
            if (opcode == IrOpcode::BitwiseAnd32 && isImmediate(other(), 1u)) return Taint::Lane;
            return std::nullopt;
    }
    return std::nullopt;
}

bool cleansUse(const IrProgram& program, const IrUse& use, Taint taint, const IrValue* mask, bool helpersOnly) {
    const auto waveSize = program.WaveSize();
    const IrValue* user = use.user;
    const auto opcode = user->Opcode();
    if (taint == Taint::Lane) {
        if (isSelect(opcode) && use.operand == 1u && implies(user->Argument(0), mask, waveSize)) return true;
        if (opcode == IrOpcode::LogicalAnd && use.operand < 2u && implies(user->Argument(1u - use.operand), mask, waveSize)) return true;
        if (readsOnlyWhereActive(opcode) && use.operand + 1u < user->ArgumentCount() && implies(user->Argument(user->ArgumentCount() - 1u), mask, waveSize)) return true;
        return helpersOnly && discardedForHelpers(opcode);
    }
    if ((taint == Taint::LowWord || taint == Taint::HighWord) && opcode == IrOpcode::BitwiseAnd32 && use.operand < 2u) {
        return clearWhereMasked(user->Argument(1u - use.operand), taint == Taint::LowWord ? 0u : 1u, mask, waveSize);
    }
    return false;
}

bool unobservedWhereMasked(const IrProgram& program, const std::unordered_set<const IrBlock*>& once, const IrValue& select, const IrValue* mask) {
    const bool fixedMask = evaluatedOnce(once, mask);
    const bool helpersOnly = excludesOnlyHelpers(program, mask);
    std::vector<std::pair<const IrValue*, Taint>> pending{{&select, Taint::Lane}};
    std::unordered_map<const IrValue*, Taint> visited{{&select, Taint::Lane}};
    while (!pending.empty()) {
        const auto [value, taint] = pending.back();
        pending.pop_back();
        for (const IrUse& use : value->OperandUses()) {
            const IrValue* user = use.user;
            if (user->Parent() == nullptr || (!fixedMask && !once.contains(user->Parent()))) return false;
            if (cleansUse(program, use, taint, mask, helpersOnly)) continue;
            const auto next = taintOfUse(program, use, taint);
            if (!next) return false;
            const auto [it, inserted] = visited.emplace(user, *next);
            if (!inserted) {
                if (it->second != *next) return false;
                continue;
            }
            if (visited.size() > VisitLimit) return false;
            pending.emplace_back(user, *next);
        }
    }
    return true;
}

}

MaskedSelectEliminationStats MaskedSelectEliminator::Eliminate(IrProgram& program) const {
    MaskedSelectEliminationStats stats;
    const auto once = blocksOutsideLoops(program);
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto blockIt = program.BlockOrder().rbegin(); blockIt != program.BlockOrder().rend(); ++blockIt) {
            const bool single = once.contains(*blockIt);
            auto& instructions = (*blockIt)->Instructions();
            auto it = instructions.end();
            while (it != instructions.begin()) {
                --it;
                IrValue* inst = *it;
                if (!isSelect(inst->Opcode()) || inst->ArgumentCount() != 3u || !inst->HasUses()) continue;
                const IrValue* mask = inst->Argument(0)->Resolve();
                if (!alwaysTrue(mask, program.WaveSize()) && ((!single && !evaluatedOnce(once, mask)) || !unobservedWhereMasked(program, once, *inst, mask))) continue;
                inst->ReplaceAllUsesWith(inst->Argument(1));
                inst->Invalidate();
                inst->SetParent(nullptr);
                it = instructions.erase(it);
                ++stats.removedSelects;
                changed = true;
            }
        }
        if (changed) DeadCodeEliminator{}.Eliminate(program);
    }
    return stats;
}

}
