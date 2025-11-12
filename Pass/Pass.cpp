#include "Pass.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include <cstdlib>
#include <ctime>

using namespace llvm;

// ---------------------------------------------------------------------------
// 辅助函数实现
// ---------------------------------------------------------------------------

CreatedBlock FetchBlock(std::vector<CreatedBlock> &haystack, BasicBlock *needle) {
    for (auto &it : haystack)
        if (needle == std::get<0>(it))
            return it;
    return std::make_tuple(nullptr, nullptr, nullptr);
}

BasicBlock *getPrimordial(std::vector<CreatedBlock> &haystack, BasicBlock *needle) {
    CreatedBlock fetched = FetchBlock(haystack, needle);
    return std::get<2>(fetched);
}

void FixupPhiNodes(std::vector<PHINode*> &PHIList,
                   std::vector<CreatedBlock> &CreatedBlocks) {
    for (PHINode *node : PHIList) {
        for (unsigned i = 0; i < node->getNumIncomingValues(); ++i) {
            BasicBlock *incoming = node->getIncomingBlock(i);
            for (BasicBlock *pred : predecessors(node->getParent())) {
                if (getPrimordial(CreatedBlocks, pred) == incoming) {
                    node->replaceIncomingBlockWith(incoming, pred);
                }
            }
        }
    }
}

void CreateNewSwitch(LLVMContext &ctx,
                     AllocaInst *Var,
                     BasicBlock *DispatcherBB,
                     std::vector<CreatedBlock> &CreatedBlocks,
                     std::vector<Instruction*> &InsList,
                     Instruction *Terminator,
                     ConstantInt *Cons) {
    IRBuilder<> B(DispatcherBB);
    LoadInst *Loaded = B.CreateLoad(Type::getInt32Ty(ctx), Var, "Var");
    // 默认使用 DispatcherBB 自身作为 default
    SwitchInst *Dispatcher = B.CreateSwitch(Loaded, DispatcherBB, InsList.size());

    ConstantInt *Next = ConstantInt::get(Type::getInt32Ty(ctx), Cons->getZExtValue());

    for (auto *InstPtr : InsList) {
        BasicBlock *BB = InstPtr->getParent();
        Dispatcher->addCase(Next, BB);

        unsigned Ran = (unsigned)rand();
        if (InstPtr != Terminator) {
            uint64_t num = Next->getZExtValue() ^ Ran;
            Next = ConstantInt::get(Type::getInt32Ty(ctx), num);
            IRBuilder<> LB(BB->getTerminator());
            Value *retVal = LB.CreateXor(
                Loaded, ConstantInt::get(Type::getInt32Ty(ctx), Ran), "ret");
            LB.CreateStore(retVal, Var);
        }
    }
}

// ---------------------------------------------------------------------------
// 主混淆逻辑
// ---------------------------------------------------------------------------
void Obfuscate(Module &M) {
    srand((unsigned)time(NULL)); // 或 srand(0) 便于调试

    for (auto &F : M) {
        if (F.isDeclaration() || F.empty())
            continue;

        outs() << "[+] Proceeding on function: " << F.getName() << "\n";
        std::vector<CreatedBlock> CreatedBlocks;

        // entry 中创建变量
        auto it = F.getEntryBlock().getFirstInsertionPt();
        IRBuilder<> entryB(&*it);
        AllocaInst *Caf = entryB.CreateAlloca(Type::getInt32Ty(F.getContext()), 0, "VarPtr");
        ConstantInt *Cons = ConstantInt::get(Type::getInt32Ty(F.getContext()), (unsigned)rand());
        entryB.CreateStore(Cons, Caf);

        for (auto &BB : F) {
            LLVMContext &Ctx = F.getContext();
            if (std::get<0>(FetchBlock(CreatedBlocks, &BB)))
                continue;

            Instruction *Terminator = BB.getTerminator();
            StoreInst *StoredLocal = nullptr;

            if (&BB != &F.getEntryBlock()) {
                IRBuilder<> BBBuilder(&*BB.getFirstInsertionPt());
                Cons = ConstantInt::get(Type::getInt32Ty(Ctx), (unsigned)rand());
                StoredLocal = BBBuilder.CreateStore(Cons, Caf);
            }

            std::vector<Instruction*> InsList;
            std::vector<PHINode*> PHIList;

            for (auto &I : BB) {
                if (&I == Caf || isa<LandingPadInst>(&I))
                    continue;
                else if (isa<PHINode>(&I)) {
                    PHIList.push_back(cast<PHINode>(&I));
                    continue;
                }
                if (auto *SI = dyn_cast<StoreInst>(&I))
                    if (SI->getPointerOperand() == Caf)
                        continue;
                InsList.push_back(&I);
            }

            // 创建 dispatcher 基本块
            BasicBlock *DispatcherBB = BasicBlock::Create(Ctx, "Dispatcher", &F);
            CreatedBlocks.push_back(std::make_tuple(DispatcherBB, nullptr, &BB));

            // 为每条指令生成新 block
            for (auto *InstPtr : InsList) {
                BasicBlock *NewBB = BasicBlock::Create(Ctx, "BB", &F);
                IRBuilder<> NB(NewBB);
                InstPtr->removeFromParent();
                NB.Insert(InstPtr);
                CreatedBlocks.push_back(std::make_tuple(NewBB, DispatcherBB, &BB));

                // 如果该指令不是 Terminator，添加跳转回 dispatcher
                if (InstPtr != Terminator)
                    BranchInst::Create(DispatcherBB, NewBB);
            }

            // 原 basic block 若无终止符，跳入 dispatcher
            if (!BB.getTerminator())
                BranchInst::Create(DispatcherBB, &BB);

            // 创建 switch dispatcher
            CreateNewSwitch(Ctx, Caf, DispatcherBB, CreatedBlocks, InsList, Terminator, Cons);

            // 修复 PHI
            if (!PHIList.empty())
                FixupPhiNodes(PHIList, CreatedBlocks);

            // 调试输出
            outs() << "  [-] Obfuscated basic block: " << BB.getName() << " ("
                   << InsList.size() << " instructions)\n";
        }
        outs() << "[+] Done with function: " << F.getName() << "\n";
    }
}
