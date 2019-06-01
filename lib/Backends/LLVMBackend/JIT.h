#ifndef BISTRA_BACKENDS_LLVMBACKEND_JIT_H
#define BISTRA_BACKENDS_LLVMBACKEND_JIT_H

#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/LambdaResolver.h"
#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/RTDyldMemoryManager.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Mangler.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Target/TargetMachine.h"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace llvm {
namespace orc {

class SimpleJIT {
public:
  using ObjLayerT = LegacyRTDyldObjectLinkingLayer;
  using CompileLayerT = LegacyIRCompileLayer<ObjLayerT, SimpleCompiler>;

  SimpleJIT()
      : Resolver(createLegacyLookupResolver(
            ES,
            [this](const std::string &Name) { return findMangledSymbol(Name); },
            [](Error Err) { cantFail(std::move(Err), "lookupFlags failed"); })),
        TM(EngineBuilder().selectTarget(llvm::Triple(), "", getHostCpuName(),
                                        getMachineAttributes())),
        DL(TM->createDataLayout()),
        ObjectLayer(ES,
                    [this](VModuleKey) {
                      return ObjLayerT::Resources{
                          std::make_shared<SectionMemoryManager>(), Resolver};
                    }),
        CompileLayer(ObjectLayer, SimpleCompiler(*TM)) {
    llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr);
  }

  TargetMachine &getTargetMachine() { return *TM; }

  VModuleKey addModule(std::unique_ptr<Module> M) {
    auto K = ES.allocateVModule();
    cantFail(CompileLayer.addModule(K, std::move(M)));
    ModuleKeys.push_back(K);
    return K;
  }

  void removeModule(VModuleKey K) {
    ModuleKeys.erase(find(ModuleKeys, K));
    cantFail(CompileLayer.removeModule(K));
  }

  JITSymbol findSymbol(const std::string Name) {
    return findMangledSymbol(mangle(Name));
  }

  /// Generate the LLVM MAttr list of attributes.
  static llvm::SmallVector<std::string, 0> getMachineAttributes() {
    llvm::SmallVector<std::string, 0> result;
    llvm::StringMap<bool> hostFeatures;
    if (llvm::sys::getHostCPUFeatures(hostFeatures)) {
      for (auto &feature : hostFeatures) {
        if (feature.second) {
          llvm::StringRef fn = feature.first();
          // Skip avx512 because LLVM does not support it well.
          if (fn.startswith("avx512")) {
            continue;
          }
          result.push_back(fn);
        }
      }
    }
    return result;
  }

  /// Returns the CPU hostname.
  static llvm::StringRef getHostCpuName() {
    auto cpu_name = llvm::sys::getHostCPUName();
    // Skip avx512 because LLVM does not support it well.
    cpu_name.consume_back("-avx512");
    return cpu_name;
  }

private:
  std::string mangle(const std::string &Name) {
    std::string MangledName;
    {
      raw_string_ostream MangledNameStream(MangledName);
      Mangler::getNameWithPrefix(MangledNameStream, Name, DL);
    }
    return MangledName;
  }

  JITSymbol findMangledSymbol(const std::string &Name) {
    const bool ExportedSymbolsOnly = true;

    // Search modules in reverse order: from last added to first added.
    // This is the opposite of the usual search order for dlsym, but makes more
    // sense in a REPL where we want to bind to the newest available definition.
    for (auto H : make_range(ModuleKeys.rbegin(), ModuleKeys.rend()))
      if (auto Sym = CompileLayer.findSymbolIn(H, Name, ExportedSymbolsOnly))
        return Sym;

    // If we can't find the symbol in the JIT, try looking in the host process.
    if (auto SymAddr = RTDyldMemoryManager::getSymbolAddressInProcess(Name))
      return JITSymbol(SymAddr, JITSymbolFlags::Exported);

    return nullptr;
  }

  ExecutionSession ES;
  std::shared_ptr<SymbolResolver> Resolver;
  std::unique_ptr<TargetMachine> TM;
  const DataLayout DL;
  ObjLayerT ObjectLayer;
  CompileLayerT CompileLayer;
  std::vector<VModuleKey> ModuleKeys;
};

} // end namespace orc
} // end namespace llvm

#endif // BISTRA_BACKENDS_LLVMBACKEND_JIT_H
