/*******************************************************************************

Copyright (C) 2017 Michal Kotoun

This file is a part of Angie project.

Angie is free software: you can redistribute it and/or modify it under the
terms of the GNU Lesser General Public License as published by the Free
Software Foundation, either version 3 of the License, or (at your option)
any later version.

Angie is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
more details.

You should have received a copy of the GNU Lesser General Public License
along with Angie.  If not, see <http://www.gnu.org/licenses/>.

*******************************************************************************/
/** @file LlvmFrontend.cpp */

#include "LlvmFrontend.hpp"

//#include "ICfgNode.hpp"
#include "CfgNodeBase.hpp"

#include "LlvmGlobals.hpp"

#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Operator.h> // OverflowingBinaryOperator etc.
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_ostream.h>


#include <llvm/IR/Constants.h>

#include <boost/utility/string_view.hpp>

LlvmCfgParser::LlvmCfgParser(
  IOperationFactory& opFactory,
  IValueContainer& vc, 
  Mapper& mapper, 
  FuncMapper& fmap
  ) 
  : 
  opFactory{opFactory}, 
  vc{vc}, 
  mapper{mapper},
  fmap{fmap}
{}
LlvmCfgParser::~LlvmCfgParser() = default;

////std::string dbgstr;
////llvm::raw_string_ostream dbgstr_rso(dbgstr);

const char* dbg_name(const llvm::Value* val)
{
  return val->getName().data();
}
const char* dbg_name(const llvm::Value& val){ return dbg_name(&val); }

const char* dbg_meta(const llvm::Instruction* val, const char* name)
{
  auto mdnode = val->getMetadata(name);
  if (!mdnode) // exists
    return "<empty>";

  std::string str{"{\n"};
  for (const auto& operand : mdnode->operands())
  {
    str.append("  ");
    str.append(((const llvm::MDString&)operand).getString());
    str.append("\n");
  }
  str.append("}\n");
  return str.data();
}
const char* dbg_meta(const llvm::Instruction& val, const char* name){ return dbg_meta(&val, name); }

extern std::string monade;

class LlvmCfgNode : public CfgNode {
private:
  const llvm::Instruction& innerInstruction;
  const bool hasBreakpoint;
  ////const OperationArgs

  /*ctr*/ LlvmCfgNode(IOperation& op, OperationArgs args,
    const llvm::Instruction& inner,
    bool bp,
    ICfgNode& prev,
    ICfgNode& next
  ) :
    innerInstruction{inner},
    hasBreakpoint{bp},
    CfgNode(op, args, prev, next) {
  }

public:

  virtual void HackMePlease() const override { monade = innerInstruction.getOpcodeName(); }
  virtual void PrintInstruction() const override { innerInstruction.print(llvm::outs()); llvm::outs() << "\n"; llvm::outs().flush();  }
  virtual void PrintLocation() const override { innerInstruction.getDebugLoc().print(llvm::outs()); llvm::outs() << "\n"; llvm::outs().flush(); }
  virtual void GetDebugInfo() const override { innerInstruction.print(llvm::errs()); llvm::errs() << "\n"; llvm::errs().flush(); }
  virtual bool HasBreakpoint() const override { return hasBreakpoint; }

  static LlvmCfgNode& CreateEmpty()
  {
    auto first = new StartCfgNode{};
    auto last = new TerminalCfgNode{};
    first->next = last;
    last->prevs.push_back(*first);
    return *(LlvmCfgNode*)first;
  }

  ////static LlvmCfgNode& CreateNode(IOperation& op, OperationArgs args, const llvm::Instruction& inner, bool bp)
  ////{
  ////  LlvmCfgNode* newNode = new LlvmCfgNode{op, args, inner, bp, *new StartCfgNode{}, *new TerminalCfgNode{}};
  ////  ((ICfgNode&)newNode->prevs[0]).next = newNode;
  ////  newNode->next->prevs.push_back(*newNode);
  ////  return *newNode;
  ////}

  //beware - adding a node after terminal node here(after improper cast) would not raise exception
  //same applies for similar linking manipulation

  LlvmCfgNode& InsertNewAfter(IOperation& op, OperationArgs args, const llvm::Instruction& inner, bool bp)
  {
    LlvmCfgNode* newNode = new LlvmCfgNode{op, args, inner, bp, *this, *this->next};
    this->next = newNode;
    return *newNode;
  }

  LlvmCfgNode& InsertNewBranchAfter(IOperation& op, OperationArgs args, const llvm::Instruction& inner, bool bp)
  {
    LlvmCfgNode* newNode = new LlvmCfgNode{op, args, inner, bp, *this, *this->next};
    newNode->nextFalse = new TerminalCfgNode{};
    this->next = newNode;
    return *newNode;
  }

  static void LinkTogetherTrue(ICfgNode& prev, ICfgNode& next)
  {
    if (prev.next->next != nullptr)
      throw std::runtime_error("prev Node has a successor other then terminal node");
    prev.next = &next;
    next.prevs.push_back(next);
  }

  static void LinkTogetherFalse(ICfgNode& prev, ICfgNode& next)
  {
    if (prev.nextFalse->next != nullptr)
      throw std::runtime_error("prev Node has a successor other then terminal node");
    prev.nextFalse = &next;
    next.prevs.push_back(next);
  }

  static void LinkTogether(ICfgNode& prev, ICfgNode& next)
  {
    LinkTogetherTrue(prev, next);
  }

  //target index specifies target when multi-target node is concerned
  //0 -> true
  //1 -> false
  //as per LLVM's getSuccessor()
  static void LinkTogether(ICfgNode& prev, ICfgNode& next, unsigned int targetIndex)
  {
    if (targetIndex == 0)
      LinkTogetherTrue(prev, next);
    else if (targetIndex == 1)
      LinkTogetherFalse(prev, next);
    else
      throw std::out_of_range("unsupported targetIndex value");
  }
};


Type LlvmCfgParser::GetValueType(llvm::Type* type)
{
  return Type{type};
}

//TODO: rename to GetId, GetFrontendId, ToId or ToFrontendId... i am in favor of ToId
FrontendId LlvmCfgParser::GetValueId(uint64_t id)
{
  return FrontendId{id};
}
FrontendId LlvmCfgParser::GetValueId(const llvm::Value* instr)
{
  return FrontendId{reinterpret_cast<uintptr_t>(instr)};
}
FrontendId LlvmCfgParser::GetValueId(const llvm::Value& instr)
{
  return FrontendId{reinterpret_cast<uintptr_t>(&instr)};
}

FrontendIdTypePair LlvmCfgParser::ToIdTypePair(const llvm::Value* value)
{
  return FrontendIdTypePair{GetValueId(value), GetValueType(value->getType())};
}
FrontendIdTypePair LlvmCfgParser::ToIdTypePair(const llvm::Value& value)
{
  return ToIdTypePair(&value);
}
OperArg LlvmCfgParser::ToOperArg(const llvm::Value* value)
{
  return OperArg{GetValueId(value), GetValueType(value->getType())};
}
OperArg LlvmCfgParser::ToOperArg(const llvm::Value& value)
{
  return ToOperArg(&value);
}
OperArg LlvmCfgParser::GetEmptyOperArg()
{
  return OperArg::CreateEmptyArg();
}

OperArg LlvmCfgParser::GetFlagsOperArg(CmpFlags flags)
{
  return OperArg{flags};
}

OperArg LlvmCfgParser::GetFlagsOperArg(ArithFlags flags)
{
  return OperArg{flags};
}

OperArg LlvmCfgParser::GetFlagsOperArg(CastOpKind kind, ArithFlags flags)
{
  return OperArg{kind, flags};
}

IOperation& LlvmCfgParser::GetOperationFor(const llvm::Instruction& instr) const
{
  ////instr.print(llvm::errs()/*, true*/);
  ////llvm::errs() << "\n";

  // Create correct operation
  IOperation* op;
  auto opcode = instr.getOpcode();
  switch (opcode)
  {
    // Terminator instructions
  case llvm::Instruction::Ret:
    op = &opFactory.Ret();
    break;
  case llvm::Instruction::Br:
    op = &opFactory.Br();
    break;

    // Memory access operations
  case llvm::Instruction::Alloca:
    op = &opFactory.Alloca();
    break;
  case llvm::Instruction::Load:
    op = &opFactory.Load();
    break;
  case llvm::Instruction::Store:
    op = &opFactory.Store();
    break;
  case llvm::Instruction::GetElementPtr:
    op = &opFactory.GetElementPtr();
    break;

    // Other operations
  case llvm::Instruction::ICmp:
  case llvm::Instruction::FCmp:
    op = &opFactory.Cmp();
    break;

  case llvm::Instruction::Call:
  {
    auto& typedInstr = static_cast<const llvm::CallInst&>(instr);
    auto func = typedInstr.getCalledFunction();
    //TODO@michkot: the following switch-like behaviour has to be duplicated in GetOperArgsForInstr
    if (func) // only if this is direct call
    {
      if (func->isIntrinsic())
      {
        if (func->getName().startswith("llvm.dbg"))
        {
          op = &opFactory.Noop();
          break;
        }
        else
        { //TODO@mikchot: guard this code to happen only if advanced-memory-operations generation are ON
          //HACK: llvm.memset and std::memset have probably different params
          if (func->getName().startswith("llvm.memset"))
          {
            op = &opFactory.Memset();
            break;
          }
          //else if (func->getName().startswith("llvm.memcpy"))
          //{
          //  op = &opFactory.Memcpy();
          //  break;
          //}
          else
          {            
            throw NotSupportedException(
              ("unsupported intrinsic function: " + func->getName().str()).c_str()
            );
          }
        }
      }
      if (func->getName().startswith("__ANGIE"))
      { // Those are analysis-independnent angie intrinsic functions, no need to guard them
        if (false) {}
        else if (func->getName() == "__ANGIE_DIAGNOSTICS_PLOT_MEMORY")
        {
          op = &opFactory.DiagnosticsPlotMem(); // In SMGS plots the memory graph
          break;
        }
        else if (func->getName().startswith(("__ANGIE_CREATE_UNKNOWN_")))
        {
          op = &opFactory.CreateUnknownVal(); // The type is deduced from target register argument
          break;
        }
        else if (func->getName().startswith(("__ANGIE_fin")))
        {
          op = &opFactory.Terminate(); // The type is deduced from target register argument
          break;
        }
#if 0 // not yet implemented
        else if (func->getName() == "__ANGIE_TRACE_PLOT")
        {
          op = &opFactory.TracePlot(); // Print the 
          break;
        }
        else if (func->getName() == "__ANGIE_FORCE_ABSTRACTION")
        {
          op = &opFactory.ForceAbstraction();
          break;
        }
        else if (func->getName() == "__ANGIE_FORCE_JOIN")
        {
          op = &opFactory.ForceJoin();
          break;
        }
#endif
      }
      else
      { //TODO@mikchot: guard this code to happen only if advanced-memory-operations generation are ON
        if (false) {}
        else if (func->getName() == "abort")
        {
          op = &opFactory.Abort();
          break;
        }
        else if (func->getName() == "exit")
        {
          op = &opFactory.Exit();
          break;
        }
        else if (func->getName() == "malloc")
        {
          op = &opFactory.Malloc();
          break;
        }
        else if (func->getName() == "free")
        {
          op = &opFactory.Free();
          break;
        }
#if 0 // not yet implemented
        else if (func->getName() == "calloc")
        {
          op = &opFactory.Calloc();
          break;
        }
        else if (func->getName() == "memset")
        {
          op = &opFactory.Memset();
          break;
        }
        else if (func->getName() == "memcpy")
        {
          op = &opFactory.Memcpy();
          break;
        }
        else if (func->getName() == "memove")
        {
          op = &opFactory.Memmove();
          break;
        }
#endif
      }
    }
    op = &opFactory.Call();
    break;
  }
  case llvm::Instruction::Invoke:
    op = &opFactory.Invoke();
    break;

  default:
    // Cast operations
    /**/ if(
      llvm::Instruction::CastOpsBegin <= opcode &&
      llvm::Instruction::CastOpsEnd   >= opcode)
    {
      op = &opFactory.Cast();
    }
    else if(
      llvm::Instruction::BinaryOpsBegin <= opcode &&
      llvm::Instruction::BinaryOpsEnd   >= opcode)
    {
      op = &opFactory.BinOp();
    }
    else
    {
      op = &opFactory.NotSupportedInstr();
    }
    break; // default:

  }


  return *op;
}


void LlvmCfgParser::constantValuesToBeCreatedInsert(const llvm::Constant* c)
{
  auto size = constantValuesToBeCreated.size();
  constantValuesToBeCreated.push_back(c);
}

// Structure of the created vector:
// 0: return value | empty arg if it its void-type
// 1: function call target | operation's cmp or arithmetic flags | empty
// 2+: operands

OperationArgs LlvmCfgParser::GetOperArgsForInstr(const llvm::Instruction& instr)
{
  std::vector<OperArg> args;

  //we know that this instructions outcome/value is never used
  //if (instr.user_empty())
  //{
  //  return args;
  //}

  //add result value, if it is not void
  if (instr.getType()->isVoidTy())
  {
    args.push_back(GetEmptyOperArg());
  }
  else
  {
    args.push_back(ToOperArg(instr));
  }

  unsigned num = instr.getNumOperands();
  switch (instr.getOpcode())
  {
  case llvm::Instruction::Call:
  {
    auto& typedInstr = static_cast<const llvm::CallInst&>(instr);
    // First, parse the call target
    auto func = typedInstr.getCalledFunction();
    if (func != nullptr)
    { // call target is a function value
      args.push_back(ToOperArg(func));
    }
    else
    { // call target is a pointer to a function of some sort
      auto val = typedInstr.getCalledValue();
      args.push_back(ToOperArg(val));
    }

    // Then, parse operands
    // Same code in default: branch
    unsigned imax = typedInstr.getNumArgOperands();
    for (unsigned i = 0; i < imax; ++i)
    {
      const auto& operand = *typedInstr.getArgOperand(i);
      if (llvm::isa<llvm::Constant>(operand))
      {
        constantValuesToBeCreatedInsert(&static_cast<const llvm::Constant&>(operand));
      }
      args.emplace_back(ToOperArg(operand));
    }

    break;
  } //  case llvm::Instruction::Call:
  case llvm::Instruction::ICmp:
  {
    auto& typedInstr = static_cast<const llvm::ICmpInst&>(instr);
    CmpFlags flags = CmpFlags::Default;
    // Signed is by default
    //flags |= typedInstr.isSigned()        ? CmpFlags::Default   : CmpFlags::Default;
    flags |= typedInstr.isUnsigned()      ? CmpFlags::Unsigned : CmpFlags::Default;
    flags |= typedInstr.isTrueWhenEqual() ? CmpFlags::Eq       : CmpFlags::Default;

    switch (typedInstr.getUnsignedPredicate())
    {
    case llvm::CmpInst::ICMP_NE:
      flags |= CmpFlags::Neq;
      break;
    case llvm::CmpInst::ICMP_UGT:
    case llvm::CmpInst::ICMP_UGE:
      flags |= CmpFlags::Gt;
      break;
    case llvm::CmpInst::ICMP_ULT:
    case llvm::CmpInst::ICMP_ULE:
      flags |= CmpFlags::Lt;
      break;
    default:
      break;
    }

    args.push_back(GetFlagsOperArg(flags));
    // Then, parse operands
    // Same code in "call": branch
    for (unsigned i = 0; i < num; ++i)
    {
      const auto& operand = *instr.getOperand(i);
      if (llvm::isa<llvm::Constant>(operand))
      {
        constantValuesToBeCreatedInsert(&static_cast<const llvm::Constant&>(operand));
      }
      args.emplace_back(ToOperArg(operand));
    }
    break;
  } // case llvm::Instruction::ICmp:
  case llvm::Instruction::FCmp:
  {
    auto& typedInstr = static_cast<const llvm::FCmpInst&>(instr);
    CmpFlags flags = CmpFlags::Default;
    flags |= CmpFlags::Float;

    throw NotImplementedException();

    args.push_back(GetFlagsOperArg(flags));
    // Then, parse operands
    // Same code in "call": branch
    for (unsigned i = 0; i < num; ++i)
    {
      const auto& operand = *instr.getOperand(i);
      if (llvm::isa<llvm::Constant>(operand))
      {
        constantValuesToBeCreatedInsert(&static_cast<const llvm::Constant&>(operand));
      }
      args.emplace_back(ToOperArg(operand));
    }
    break;
  } // case llvm::Instruction::ICmp:
  case llvm::Instruction::GetElementPtr:
  {
    auto& typedInstr = static_cast<const llvm::GetElementPtrInst&>(instr);
    llvm::APInt constantOffset{64, 0};
    if (typedInstr.accumulateConstantOffset(llvmDataLayout, constantOffset))
    {
      args.push_back(OperArg{FrontendId{constantOffset.getZExtValue()}, Type::CreateVoidType()});
    }
    else
    {
      args.push_back(GetEmptyOperArg());
    }
    // Then, parse operands
    // Same code in "call": branch
    for (unsigned i = 0; i < num; ++i)
    {
      const auto& operand = *instr.getOperand(i);
      if (llvm::isa<llvm::Constant>(operand))
      {
        constantValuesToBeCreatedInsert(&static_cast<const llvm::Constant&>(operand));
      }
      args.emplace_back(ToOperArg(operand));
    }

    break;
  } //  case llvm::Instruction::GetElementPtr:
  default:
  {
    /**/ if (llvm::isa<llvm::BinaryOperator>(instr))
    {
      auto& typedInstr = static_cast<const llvm::BinaryOperator&>(instr);

      ArithFlags   flags  = ArithFlags::Default;
      BinaryOpKind opKind = BinaryOpKind::Default;

      if (llvm::isa<llvm::OverflowingBinaryOperator>(instr))
      {
        flags |= typedInstr.hasNoSignedWrap() ? ArithFlags::NoSignedWrap : ArithFlags::Default;
        flags |= typedInstr.hasNoUnsignedWrap() ? ArithFlags::NoUnsignedWrap : ArithFlags::Default;
      }
      //TODO@nobody: Add more? - probably not

      switch (static_cast<llvm::Instruction::BinaryOps>(instr.getOpcode()))
      {
      // Binary instructions
      case llvm::Instruction::Add:
      case llvm::Instruction::FAdd:
        opKind = BinaryOpKind::Add;
        break;
      case llvm::Instruction::Sub:
      case llvm::Instruction::FSub:
        opKind = BinaryOpKind::Sub;
        break;
      case llvm::Instruction::Mul:
      case llvm::Instruction::FMul:
        opKind = BinaryOpKind::Mul;
        break;
      case llvm::Instruction::UDiv:
      case llvm::Instruction::SDiv:
      case llvm::Instruction::FDiv:
        opKind = BinaryOpKind::Div;
        break;
      case llvm::Instruction::URem:
      case llvm::Instruction::SRem:
      case llvm::Instruction::FRem:
        opKind = BinaryOpKind::Rem;
        break;

      // Bitwise binary instructions
      case llvm::Instruction::Shl:
        opKind = BinaryOpKind::Shl;
        break;
      case llvm::Instruction::LShr:
      case llvm::Instruction::AShr:
        opKind = BinaryOpKind::Shr;
        break;
      case llvm::Instruction::And:
        opKind = BinaryOpKind::And;
        break;
      case llvm::Instruction::Or:
        opKind = BinaryOpKind::Or;
        break;
      case llvm::Instruction::Xor:
        opKind = BinaryOpKind::Xor;
        break;
      case llvm::Instruction::BinaryOps::BinaryOpsEnd:
        break;
      }

      switch (instr.getOpcode())
      {
      case llvm::Instruction::SDiv:
      case llvm::Instruction::AShr:
        flags |= typedInstr.isExact() ? ArithFlags::Exact : ArithFlags::Default;
        // no break!
      case llvm::Instruction::SRem:
        flags |= ArithFlags::Signed;
        break;
      case llvm::Instruction::UDiv:
      case llvm::Instruction::LShr:
        flags |= typedInstr.isExact() ? ArithFlags::Exact : ArithFlags::Default;
        // no break!
      case llvm::Instruction::URem:
        flags |= ArithFlags::Unsigned;
        break;
      default:
        break;
      }

      args.push_back(OperArg{opKind, flags});
    }
    else if (llvm::isa<llvm::CastInst>(instr))
    {
      CastOpKind opKind = CastOpKind::Default;
      ArithFlags flags  = ArithFlags::Default;

      switch (static_cast<llvm::Instruction::CastOps>(instr.getOpcode()))
      {
      case llvm::Instruction::CastOps::Trunc:
        opKind = CastOpKind::Truncate;
        break;
      case llvm::Instruction::CastOps::ZExt:
        flags  = ArithFlags ::Unsigned;
        opKind = CastOpKind::Extend;
        break;
      case llvm::Instruction::CastOps::SExt:
        flags  = ArithFlags ::Signed;
        opKind = CastOpKind::Extend;
        break;
      case llvm::Instruction::CastOps::FPExt:
        opKind = CastOpKind::Extend;
        break;
      case llvm::Instruction::CastOps::BitCast:
        opKind = CastOpKind::BitCast;
        break;
      case llvm::Instruction::CastOps::PtrToInt:
        opKind = CastOpKind::PtrToInt;
        break;
      case llvm::Instruction::CastOps::IntToPtr:
        opKind = CastOpKind::IntToPtr;
        break;
      default:
        //TODO@michkot Add some info level debugging warning, when other (FP or address space) casts occur
        break;
      }
      args.push_back(OperArg{opKind, flags});
    }
    else // add empty operand placeholder for flags/function target
    {
      args.push_back(GetEmptyOperArg());
    }

    // Then, parse operands
    // Same code in "call": branch
    for (unsigned i = 0; i < num; ++i)
    {
      const auto& operand = *instr.getOperand(i);
      if (llvm::isa<llvm::Constant>(operand))
      {
        constantValuesToBeCreatedInsert(&static_cast<const llvm::Constant&>(operand));
      }
      args.emplace_back(ToOperArg(operand));
    }

    break;
  } // default:
  } // switch

  return OperationArgs{std::move(args)};
}

bool LlvmCfgParser::TryGetMappedCfgNode(const llvm::BasicBlock* bb, LlvmCfgNode** outNode)
{
  auto assignedNodeIt = copyMapping.find(bb);
  if (assignedNodeIt != copyMapping.end())
  {
    *outNode = (*assignedNodeIt).second;
    return true;
  }
  return false;
}

void LlvmCfgParser::SetMappedCfgNode(const llvm::BasicBlock* bb, LlvmCfgNode* outNode)
{
  copyMapping[bb] = outNode;
}

void LlvmCfgParser::LinkWithOrPlanProcessing(
    LlvmCfgNode* currentNode,
    const llvm::BasicBlock* nextBlock,
    unsigned targetIndex
    )
{
  LlvmCfgNode* nextBlockNode;
  if (TryGetMappedCfgNode(nextBlock, &nextBlockNode))
  { //this block was already parsed before
    //just link it together
    LlvmCfgNode::LinkTogether(*currentNode, *nextBlockNode, targetIndex);
  }
  else
  { //we need to plan parsing of this block and link it when done
    parseAndLinkTogether.emplace(nextBlock, currentNode, targetIndex);
  }
}

LlvmCfgNode& LlvmCfgParser::ParseBasicBlock(const llvm::BasicBlock* entryBlock)
{
  LlvmCfgNode* currentNode{nullptr};
  //if the BB was already parsed
  if (TryGetMappedCfgNode(entryBlock, &currentNode))
    return *currentNode;

  //first BB instruction
  auto instrPtr = &entryBlock->front();

  //create empty CFG
  LlvmCfgNode& zeroNode = LlvmCfgNode::CreateEmpty();
  currentNode = &zeroNode;

  //all instructions in BB except for the last one
  //add them to CFG
  while (!instrPtr->isTerminator())
  {
    auto& op = GetOperationFor(*instrPtr);
    auto args = GetOperArgsForInstr(*instrPtr);
    bool bp = (instrPtr->getMetadata("angie.debugbreak") != nullptr);
    currentNode = &currentNode->InsertNewAfter(op, args, *instrPtr, bp);
    instrPtr = instrPtr->getNextNode();
  }
  //last BB instruction
  {
    // If the instruction is terminator, links go to the next block
    //TODO@michkot: Supports only Br instr. (conditional and unconditional)

    // get matching op
    auto& op = GetOperationFor(*instrPtr);
    // and op's arguments
    auto args = GetOperArgsForInstr(*instrPtr);

    bool bp = (instrPtr->getMetadata("angie.debugbreak") != nullptr);

    switch (instrPtr->getOpcode())
    {
    case llvm::Instruction::Br:
    {
      auto branchInstrPtr = static_cast<const llvm::BranchInst*>(instrPtr);

      if (branchInstrPtr->isUnconditional())
      {
        // Unconditional branch - next is first instruction of target basic block

        // Place it as noop unconditionally, serves only for dbg printing
        currentNode = &currentNode->InsertNewAfter(opFactory.Noop(), args, *instrPtr, bp);

        LinkWithOrPlanProcessing(currentNode, branchInstrPtr->getSuccessor(0), 0);
      }
      else //if (branchInstrPtr->isConditional())
      {
        // Conditional branch - has two successors for true and false branches
        currentNode = &currentNode->InsertNewBranchAfter(op, args, *instrPtr, bp);
        LinkWithOrPlanProcessing(currentNode, branchInstrPtr->getSuccessor(0), 0);
        LinkWithOrPlanProcessing(currentNode, branchInstrPtr->getSuccessor(1), 1);
      }
    }
    break;
    case llvm::Instruction::Ret:
    {
      // Create node for RET instruction, do not link with anything
      currentNode = &currentNode->InsertNewAfter(op, args, *instrPtr, bp);
    }
    break;
    default:
      break;
    } // end of switch

  }

  auto& firstNode = (LlvmCfgNode&)zeroNode.GetNext();
  SetMappedCfgNode(entryBlock, &firstNode);
  return firstNode;
}

void LlvmCfgParser::DealWithConstants()
{
  for (auto x : constantValuesToBeCreated)
  {
    ValueId id;
    /**/ if (auto constInt = llvm::dyn_cast<llvm::ConstantInt>(x))
    {
        id = vc.CreateConstIntVal(constInt->getZExtValue(), Type{constInt->getType()});
    }
    else if (auto constFp = llvm::dyn_cast<llvm::ConstantFP>(x))
    {
      if (&constFp->getValueAPF().getSemantics() == &llvm::APFloat::IEEEdouble())
      {
        id = vc.CreateConstFloatVal(constFp->getValueAPF().convertToDouble(), Type{constFp->getType()});
      }
      else if (&constFp->getValueAPF().getSemantics() == &llvm::APFloat::IEEEsingle())
      {
        id = vc.CreateConstFloatVal(constFp->getValueAPF().convertToFloat(), Type{constFp->getType()});
      }
      else
        throw NotSupportedException("unsupported floating point constant");
    }
    else if (auto constNullPtr = llvm::dyn_cast<llvm::ConstantPointerNull>(x))
    {
      //TODO maybe different value than 0 for nullptr?
      id = vc.CreateConstIntVal(0, Type{constNullPtr->getType()});
    }
    else if (auto constExpr = llvm::dyn_cast<llvm::ConstantExpr>(x))
    {
      // this instruction is freely floating and needs to by destroyed! 
      llvm::Instruction* asInstr = deconst_cast(*constExpr).getAsInstruction();
      if (false) {}
      else if (llvm::isa<llvm::CastInst>(asInstr) && asInstr->getNumOperands() == 1)
      {
        auto asConstInt = llvm::dyn_cast<llvm::ConstantInt>(asInstr->getOperand(0));
        id = vc.CreateConstIntVal(asConstInt->getZExtValue(), Type{constExpr-> getType()});
      }
      else if (llvm::isa<llvm::GetElementPtrInst>(asInstr) && asInstr->getNumOperands() == 3)
      {
        auto idx0 = llvm::dyn_cast<llvm::ConstantInt>(asInstr->getOperand(1));
        auto idx1 = llvm::dyn_cast<llvm::ConstantInt>(asInstr->getOperand(2));
        if (idx0->getZExtValue() == 0 && idx1->getZExtValue() == 0)
        {
          //TODO: we are creating pointer to the begining of constant (string?) - relay it to SMGs
          //HACK: we are replacig pointer to static value with unknown
          id = vc.CreateVal(Type{constExpr-> getType()});
        }
        else
          throw NotSupportedException("unsupported constant constexpr");
      }
      else
        throw NotSupportedException("unsupported constant constexpr");
      asInstr->deleteValue();
    }
    else if (auto constFunc = llvm::dyn_cast<llvm::Function>(x))
    {
      id = mapper.GetValueId(GetValueId(constFunc));
    }
    else if (auto constOther = llvm::dyn_cast<llvm::GlobalVariable>(x))
    {
      throw NotSupportedException("global variables are not supported yet");
    }
    else
      throw NotImplementedException("'other constan value types' ");

    mapper.LinkToValueId(GetValueId(x), id);
  }
}

ICfgNode& LlvmCfgParser::ParseFunction(const llvm::Function& func)
{
  // Get first instruction of function
  auto& entryBlock = func.getEntryBlock();
  return ParseBasicBlock(&entryBlock);
}

void LlvmCfgParser::ParseModule(llvm::Module& module)
{
  auto& ctx = module.getContext();
  auto mainFunc = module.getFunction("main");

  // -----------

  if (module.getFunctionList().size() == 0)
  {
    throw std::runtime_error(
      "The input LLVM IR module contains zero functions -- compilation probably failed");
  }

  for (auto& func : module.functions())
  {
    if (func.isDeclaration())
      continue;
    auto& cfg    = ParseFunction(func);
    auto  params = OperationArgs{};
    for (auto& param : func.args()) // Added all formal arguments
      params.GetArgs().push_back(ToOperArg(param));
    auto  info   = FunctionInfo{};
    auto  handle = std::make_unique<FunctionHandle>(cfg, std::move(params), info);
    auto  vid    = mapper.CreateOrGetValueId(ToIdTypePair(func), vc);
    fmap.RegisterFunction(vid, std::move(handle));
  }

  mainCfg = &fmap.GetFunction(mapper.GetValueId(GetValueId(mainFunc))).cfg;

  // -----------

  auto mainFType  = mainFunc->getFunctionType();

  // -----------

  // the following code prepends a fixed void __entry(int32, int8**) function to the module
  // this is fixed to C/C++ and needs to be modulirazied to support other languages
  // the purpose of "__entry" is to add here any instructions constraining the argc and argv values
  //   before they are used as arguments for "main"
  // it can also have the "termination" check purpose - if the end of __entry is not reached

  auto voidType   = llvm::Type::getVoidTy(ctx);
  auto params     = llvm::SmallVector<llvm::Type*, 2>{
      llvm::Type::getInt32Ty(ctx), 
      llvm::Type::getInt8PtrTy(ctx)->getPointerTo()
    };
  auto entryFType = llvm::FunctionType::get(voidType, params, false);
  auto finFType   = llvm::FunctionType::get(voidType, false);

  auto finFunc    = llvm::Function::Create(finFType,   llvm::GlobalValue::LinkageTypes::AvailableExternallyLinkage, "__ANGIE_fin",   &module);
  auto entryFunc  = llvm::Function::Create(entryFType, llvm::GlobalValue::LinkageTypes::AvailableExternallyLinkage, "__entry", &module);

  auto entryBlock = llvm::BasicBlock::Create(ctx, "", entryFunc);

  // -----------

  // Add instructions to maniuplate / create the call args for main

  // Default way is to create them as __entry arguments
  auto callValues = entryFunc->args();

  ////auto argcType   = mainFType->getParamType(0);
  ////auto argvType   = mainFType->getParamType(1);
  ////auto argc       = llvm::BinaryOperator::CreateXor(
  ////  llvm::Constant::getNullValue(argcType),
  ////  llvm::Constant::getNullValue(argcType),
  ////  "",
  ////  entryBlock
  ////  );
  ////auto argv       = llvm::BinaryOperator::CreateXor(
  ////  llvm::Constant::getNullValue(argvType),
  ////  llvm::Constant::getNullValue(argvType),
  ////  "",
  ////  entryBlock
  ////  );

  // -----------

  // place for main call args ... count is going to be either:
  //   0 for int main(void)
  //   2 for int main(int, const char**)
  auto callArgs   = llvm::SmallVector<llvm::Value*, 2>{};

  auto opArgs     = OperationArgs{};
  opArgs.GetArgs().push_back(GetEmptyOperArg());
  opArgs.GetArgs().push_back(ToOperArg(mainFunc));

  // for every formal param, add one on of the values as main call arg
  auto argIt = callValues.begin();
  for (auto mainParam : mainFType->params())
  {
    auto arg = &*argIt++;
    // Add to the function call arg range
    callArgs.push_back(arg);
    // Add to the operation
    opArgs.GetArgs().push_back(ToOperArg(arg));
    // Register args as values
    mapper.CreateOrGetValueId(ToOperArg(arg).idTypePair, vc);
  }

  // -----------
  
  auto mainCall   = llvm::CallInst::Create(mainFunc, callArgs, "", entryBlock);
  auto finCall    = llvm::CallInst::Create(finFunc,            "", entryBlock);

  // -----------

  auto voidOpArgs = OperationArgs{};
  voidOpArgs.GetArgs().push_back(GetEmptyOperArg());

  auto currtNode  = &LlvmCfgNode::CreateEmpty();
  currtNode  = &currtNode->InsertNewAfter(opFactory.Call(),      opArgs,     *mainCall, false);
  entryPointCfg = currtNode;
  currtNode  = &currtNode->InsertNewAfter(opFactory.Terminate(), voidOpArgs, *finCall,  false);
 
  // -----------

  while (!parseAndLinkTogether.empty())
  {
    const llvm::BasicBlock* blockToParse;
    LlvmCfgNode* prevNodeToLink;
    unsigned targetIndexInNode;

    std::tie(blockToParse, prevNodeToLink, targetIndexInNode) = parseAndLinkTogether.front();
    parseAndLinkTogether.pop();

    auto& blockStartNode = ParseBasicBlock(blockToParse);
    LlvmCfgNode::LinkTogether(*prevNodeToLink, blockStartNode, targetIndexInNode);
  }

  DealWithConstants();
}

uptr<llvm::Module> LlvmCfgParser::OpenIrFile(std::string fileName)
{
  llvm::SMDiagnostic err;
  llvm::LLVMContext &context = *new llvm::LLVMContext();

  auto module = llvm::parseIRFile(fileName, err, context);
  if (module == nullptr)
  {
    std::runtime_error msg{err.getMessage().str()};
    throw msg;
    exit(1);
  }
  return module;
}

void LlvmCfgParser::ParseAndOpenIrFile(boost::string_view fileName)
{
  moduleHandle = OpenIrFile(fileName.to_string());
  setLlvmGlobalVars(&*moduleHandle);
  ParseModule(*moduleHandle);
}
