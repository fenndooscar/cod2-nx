/**
 * @file        codegen/phase_discover.cpp
 * @brief       Discover phase: iterative function block discovery
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include "codegen_flags.h"
#include "decoded_binary.h"
#include <rex/codegen/function_scanner.h>

#include <array>
#include <bitset>
#include <unordered_set>

#include <rex/codegen/phases.h>
#include "phase_helpers.h"

#include <rex/codegen/vtable_scanner.h>
#include <rex/logging.h>

#include "codegen_logging.h"
#include <rex/memory/utils.h>

#include <ppc.h>

using rex::codegen::ppc::Opcode;
using rex::memory::load_and_swap;

namespace rex::codegen {

namespace {

// Defined below, next to functionPointerScan; called from discoverAllFunctions.
void dataPointerScan(CodegenContext& ctx);

//=============================================================================
// Discover Phase: iterative function block discovery
//=============================================================================

void discoverFunction(CodegenContext& ctx, uint32_t funcAddr,
                      const std::unordered_set<uint32_t>& knownFunctions) {
  auto& graph = ctx.graph;
  auto& binary = ctx.binary();
  auto& decoded = ctx.decoded();

  auto* node = graph.getFunction(funcAddr);
  if (!node)
    return;

  // Skip if already discovered
  if (!node->canDiscover()) {
    REXCODEGEN_TRACE("Analyze: function 0x{:08X} already discovered, skipping", funcAddr);
    return;
  }

  // Imports don't need block discovery
  if (node->isImport()) {
    node->discoverAsImport();
    return;
  }

  REXCODEGEN_TRACE("Analyze: discovering function 0x{:08X} ({})", funcAddr, node->name());

  // Lookup pdataSize for exception handler boundary
  uint32_t pdataSize = 0;

  // For CONFIG functions: use only the explicitly declared size (if any)
  // If no size specified (size=0), let discovery find natural boundaries via region
  // Don't inherit PDATA sizes for CONFIG functions - they're user hints for entry points
  if (node->authority() == FunctionAuthority::CONFIG) {
    pdataSize = node->size();  // 0 if not specified, which is correct
    REXCODEGEN_TRACE("Analyze: 0x{:08X} is CONFIG, using declared size={}", funcAddr, pdataSize);
  } else {
    // For non-CONFIG functions, use PDATA size if available
    auto pdataIt = ctx.scan.pdataSizes.find(funcAddr);
    if (pdataIt != ctx.scan.pdataSizes.end()) {
      pdataSize = pdataIt->second;
      REXCODEGEN_TRACE("Analyze: 0x{:08X} using PDATA size={}", funcAddr, pdataSize);
    }
  }

  // Find the code region containing this function
  const CodeRegion* region = nullptr;
  for (const auto& r : ctx.scan.codeRegions) {
    if (r.contains(funcAddr)) {
      region = &r;
      break;
    }
  }
  if (!region) {
    REXCODEGEN_WARN("Analyze: function 0x{:08X} not in any code region", funcAddr);
    return;
  }

  // Pass pdataSize so forward branches within function extent are correctly identified
  auto result = discoverBlocks(decoded, funcAddr, *region, knownFunctions, pdataSize,
                               &ctx.Config().switchTables);

  if (result.blocks.empty()) {
    REXCODEGEN_WARN("Analyze: no blocks found for function 0x{:08X}", funcAddr);
    return;
  }

  // snooper the function with the discovered blocks and instructions
  node->discover(std::move(result.blocks), std::move(result.instructions),
                 std::move(result.labels));

  // Add jump tables (targets become labels in the function)
  for (const auto& jt : result.jumpTables) {
    graph.addJumpTableToFunction(funcAddr, jt);
  }

  // Register external call targets as new functions (bl only, not b)
  for (uint32_t target : result.externalCalls) {
    if (!graph.isEntryPoint(target) && !graph.isImport(target)) {
      if (binary.isInImportExportRange(target)) {
        continue;
      }
      graph.addFunction(target, 4, FunctionAuthority::DISCOVERED, true);
    }
  }

  // Add unresolved branches for later resolution
  for (const auto& branch : result.unresolvedBranches) {
    graph.addUnresolvedJumpToFunction(funcAddr, branch.site, branch.target, branch.isCall,
                                      branch.isConditional);
  }

  // Scan exception handler regions for branches not in discovered blocks
  if (pdataSize > 0) {
    std::unordered_set<uint32_t> discoveredAddrs;
    for (const auto& block : result.blocks) {
      for (uint32_t addr = block.base; addr < block.base + block.size; addr += 4) {
        discoveredAddrs.insert(addr);
      }
    }

    uint32_t pdataEnd = funcAddr + pdataSize;
    const uint8_t* funcData = binary.translate(funcAddr);
    if (funcData) {
      for (uint32_t offset = 0; offset < pdataSize; offset += 4) {
        uint32_t site = funcAddr + offset;

        // Skip if already discovered by normal control flow
        if (discoveredAddrs.count(site))
          continue;

        // Skip if marked invalid
        auto invalidIt = ctx.analysisState().invalidInstructions.find(site);
        if (invalidIt != ctx.analysisState().invalidInstructions.end()) {
          continue;
        }

        uint32_t insn = load_and_swap<uint32_t>(funcData + offset);
        uint32_t opcode = PPC_OP(insn);

        if (opcode != PPC_OP_B && opcode != PPC_OP_BC)
          continue;

        uint32_t target = 0;
        bool isCall = PPC_BL(insn);
        bool isAbsolute = PPC_BA(insn);

        if (opcode == PPC_OP_B) {
          int32_t branchOffset = PPC_BI(insn);
          target = isAbsolute ? static_cast<uint32_t>(branchOffset) : site + branchOffset;
        } else {
          int32_t branchOffset = PPC_BD(insn);
          target = isAbsolute ? static_cast<uint32_t>(branchOffset) : site + branchOffset;
        }

        // Skip internal jumps within pdata region
        if (!isCall && target >= funcAddr && target < pdataEnd) {
          continue;
        }

        graph.addUnresolvedJumpToFunction(funcAddr, site, target, isCall, false);

        // Register call targets as new functions
        if (isCall && !graph.isEntryPoint(target) && !graph.isImport(target)) {
          if (binary.isInImportExportRange(target)) {
            continue;
          }
          graph.addFunction(target, 4, FunctionAuthority::DISCOVERED, true);
        }
      }
    }
  }
}

void discoverAllFunctions(CodegenContext& ctx) {
  REXCODEGEN_TRACE("Analyze: starting iterative discovery...");

  auto& graph = ctx.graph;
  auto& binary = ctx.binary();

  // Iterative discovery
  size_t iteration = 0;
  size_t lastFunctionCount = 0;
  const size_t maxIterations = REXCVAR_GET(max_discovery_iterations);

  while (iteration < maxIterations) {
    iteration++;

    size_t currentFunctionCount = graph.functionCount();
    if (currentFunctionCount == lastFunctionCount && iteration > 1) {
      REXCODEGEN_DEBUG("Analyze: fixed point at iteration {} ({} functions)", iteration,
                       currentFunctionCount);
      break;
    }

    lastFunctionCount = currentFunctionCount;

    auto knownFunctions = buildKnownFunctions(graph);
    if (discoverPendingFunctions(ctx, knownFunctions) == 0) {
      break;
    }
  }

  REXCODEGEN_TRACE("Analyze: {} functions after call graph expansion", graph.functionCount());

  // VTable scanning
  {
    VTableScanner vtScanner(binary);
    auto vtables = vtScanner.scan();

    size_t newFunctions = 0;

    for (const auto& vt : vtables) {
      for (size_t i = 0; i < vt.slots.size(); i++) {
        uint32_t funcAddr = vt.slots[i];

        if (graph.isEntryPoint(funcAddr))
          continue;
        if (binary.isInImportExportRange(funcAddr))
          continue;

        graph.addFunction(funcAddr, 4, FunctionAuthority::VTABLE, true);
        newFunctions++;
      }
    }

    REXCODEGEN_TRACE("Analyze: VTable scan found {} vtables, {} new functions", vtables.size(),
                     newFunctions);

    // Continue discovery for vtable functions
    if (newFunctions > 0) {
      size_t vtableIteration = 0;
      const size_t maxVtableIterations = REXCVAR_GET(max_vtable_iterations);

      while (vtableIteration < maxVtableIterations) {
        vtableIteration++;

        auto knownFunctions = buildKnownFunctions(graph);
        if (discoverPendingFunctions(ctx, knownFunctions) == 0)
          break;

        if (graph.functionCount() == lastFunctionCount)
          break;
        lastFunctionCount = graph.functionCount();
      }
    }
  }

  REXCODEGEN_TRACE("Analyze: {} total functions after vtable scan", graph.functionCount());

  // Entry points that only a data pointer reaches. This runs after the call
  // graph and the vtable scan have settled, so the pointers that name a
  // function already known are skipped and only the missed ones are added.
  // Their blocks are discovered in the same pass, which is why this belongs
  // here and not in analyze.cpp next to the disabled functionPointerScan.
  {
    size_t before = graph.functionCount();
    if (REXCVAR_GET(data_pointer_scan)) {
      dataPointerScan(ctx);
    }
    if (graph.functionCount() != before) {
      size_t iteration = 0;
      const size_t maxIterations = REXCVAR_GET(max_vtable_iterations);
      while (iteration < maxIterations) {
        iteration++;
        auto knownFunctions = buildKnownFunctions(graph);
        if (discoverPendingFunctions(ctx, knownFunctions) == 0) {
          break;
        }
        if (graph.functionCount() == lastFunctionCount) {
          break;
        }
        lastFunctionCount = graph.functionCount();
      }
    }
  }

  REXCODEGEN_TRACE("Analyze: {} total functions after data pointer scan",
                   graph.functionCount());
}

//=============================================================================
// Function Pointer Scan: find lis/addi pairs loading code addresses
// TODO(tomc): THIS IS WIP AND PROB A BAD IDEA LOL LETS SEE
//=============================================================================
void functionPointerScan(CodegenContext& ctx) {
  if (!ctx.hasDecoded()) {
    REXCODEGEN_WARN("functionPointerScan: DecodedBinary not initialized, skipping");
    return;
  }

  auto& graph = ctx.graph;
  auto& decoded = ctx.decoded();
  const auto& codeRegions = decoded.codeRegions();

  if (codeRegions.empty()) {
    REXCODEGEN_WARN("functionPointerScan: no code regions, skipping");
    return;
  }

  // Build set of existing functions to avoid duplicates
  std::unordered_set<uint32_t> existingFunctions;
  for (const auto& [addr, node] : graph.functions()) {
    existingFunctions.insert(addr);
  }

  // Track lis values: register -> (high_value, lis_address)
  // We scan linearly and track the most recent lis for each register
  // PPC has exactly 32 GPRs, so a fixed-size array is more efficient than a map
  std::array<std::pair<uint32_t, uint32_t>, 32> lisValues{};
  std::bitset<32> lisValid;

  size_t foundCount = 0;

  for (const auto& region : codeRegions) {
    lisValid.reset();  // Reset tracking at region boundaries

    for (uint32_t addr = region.start; addr < region.end; addr += 4) {
      auto* insn = decoded.get(addr);
      if (!insn)
        continue;

      // Track lis rD, IMM
      if (isLis(*insn)) {
        uint8_t rd = static_cast<uint8_t>(insn->D.RT);
        uint32_t hi = static_cast<uint32_t>(static_cast<int16_t>(insn->D.d)) << 16;
        lisValues[rd] = {hi, addr};
        lisValid.set(rd);
        continue;
      }

      // Check for addi rD, rA, IMM where rA was set by lis
      if (insn->opcode == rex::codegen::ppc::Opcode::addi) {
        uint8_t ra = static_cast<uint8_t>(insn->D.RA);
        if (ra == 0)
          continue;  // li pseudo-op, not addi

        if (!lisValid.test(ra))
          continue;

        uint32_t hi = lisValues[ra].first;
        int16_t lo = static_cast<int16_t>(insn->D.d);
        uint32_t fullAddr = hi + lo;  // Sign-extended add

        // PPC instructions are 4-byte aligned
        if (fullAddr & 0x3)
          continue;

        // Check if this address is in a code region
        const CodeRegion* targetRegion = decoded.regionContaining(fullAddr);
        if (!targetRegion)
          continue;

        // Skip if already a known function
        if (existingFunctions.contains(fullAddr))
          continue;

        // Skip if it's an internal address (within same function's likely range)
        // Heuristic: if target is very close to current address, probably internal label
        int32_t distance = static_cast<int32_t>(fullAddr) - static_cast<int32_t>(addr);
        if (distance > -0x1000 && distance < 0x1000) {
          // Could be local label, skip for now
          continue;
        }

        // Register as function with DISCOVERED authority and hasXrefs=true
        graph.addFunction(fullAddr, 4, FunctionAuthority::DISCOVERED, true);
        existingFunctions.insert(fullAddr);
        foundCount++;

        REXCODEGEN_TRACE("functionPointerScan: found 0x{:08X} via lis/addi at 0x{:08X}", fullAddr,
                         addr);
      }

      // Also check ori rD, rA, IMM (alternative to addi for unsigned)
      if (insn->opcode == rex::codegen::ppc::Opcode::ori) {
        uint8_t ra = static_cast<uint8_t>(insn->D.RA);
        if (!lisValid.test(ra))
          continue;

        uint32_t hi = lisValues[ra].first;
        uint16_t lo = static_cast<uint16_t>(insn->D.d);
        uint32_t fullAddr = hi | lo;  // Unsigned OR

        // PPC instructions are 4-byte aligned
        if (fullAddr & 0x3)
          continue;

        const CodeRegion* targetRegion = decoded.regionContaining(fullAddr);
        if (!targetRegion)
          continue;

        if (existingFunctions.contains(fullAddr))
          continue;

        int32_t distance = static_cast<int32_t>(fullAddr) - static_cast<int32_t>(addr);
        if (distance > -0x1000 && distance < 0x1000)
          continue;

        graph.addFunction(fullAddr, 4, FunctionAuthority::DISCOVERED, true);
        existingFunctions.insert(fullAddr);
        foundCount++;

        REXCODEGEN_TRACE("functionPointerScan: found 0x{:08X} via lis/ori at 0x{:08X}", fullAddr,
                         addr);
      }

      // Clear lis tracking if register is overwritten by other instruction
      // (Simplified: we clear on any write to the register)
      // This is conservative - could miss some patterns but avoids false positives
    }
  }

  REXCODEGEN_TRACE("functionPointerScan: found {} new function pointer targets", foundCount);
}

//=============================================================================
// Data Pointer Scan: find function entry points that only a pointer reaches
//
// The scan above only looks at code, and only at lis/addi pairs. It therefore
// misses the way Call of Duty 2 (and every MSVC-built 360 title) stores its
// initializer tables: a plain array of addresses in .rdata, walked at startup
// and each entry called through a register. Nothing in the code names those
// functions, so no amount of branch following finds them, and the game dies at
// the first one with
//
//     [FATAL] Call to invalid or unregistered function at guest address 0x...
//
// This is what made that crash appear to move: each declared address uncovered
// the next entry of the same table.
//
// A dword in a data section that lands inside a code region is a function entry
// point, by construction - there is nothing else it can be. The only judgement
// call is which sections count, and the answer is the non-executable ones that
// the linker fills with addresses: .rdata and .data.
//
// The one thing that breaks "by construction" is a label of a function that is
// already discovered. The compiler also stores those in .rdata, and they are not
// entry points at all: three consecutive dwords at 0x82068A14 name 0x8229AF54,
// 0x8229AF00 and 0x8229AFB4, which are three branch targets inside the loop of
// sub_8229AED0. Declaring them as functions starts a second decode in the middle
// of a working one, and the real function's own backward branch then names an
// address that belongs to the other function:
//
//     Unresolved function 0x8229AF10 from 0x8229AFC8 (no CallTarget in FunctionNode)
//
// which is emitted as REX_FATAL in the middle of the loop instead of the loop.
// That was the whole of the nine unresolved branches this scan left behind.
//=============================================================================
void dataPointerScan(CodegenContext& ctx) {
  auto& graph = ctx.graph;
  const auto& binary = ctx.binary();

  std::unordered_set<uint32_t> existingFunctions;
  // The code every function already discovered owns, as [start, end) intervals.
  // A candidate inside one of them is not an entry point: it is code the
  // function already has, and declaring it starts a second decode in the middle
  // of a working one. This is containsAddress()'s rule, precomputed because the
  // scan tests ~1200 candidates against ~21000 functions.
  //
  // It is not enough to test labels(): 0x8229AF00 is the instruction after a bl
  // inside sub_8229AED0, so it starts a block but is nobody's branch target and
  // never reaches the label set. 0x8229AFB4, two instructions of the same loop,
  // is a beq target and does. Both are fragments of the same function.
  std::vector<std::pair<uint32_t, uint32_t>> ownedRanges;
  for (const auto& [addr, node] : graph.functions()) {
    existingFunctions.insert(addr);
    for (const auto& block : node->blocks()) {
      ownedRanges.emplace_back(block.base, block.end());
    }
    // CONFIG and PDATA functions declare a size their blocks need not cover
    // (out-of-line switch cases); containsAddress() trusts it, so this must too.
    if (node->authority() == FunctionAuthority::CONFIG ||
        node->authority() == FunctionAuthority::PDATA) {
      ownedRanges.emplace_back(node->base(), node->end());
    }
  }
  std::sort(ownedRanges.begin(), ownedRanges.end());
  auto ownedByAFunction = [&ownedRanges](uint32_t addr) {
    auto it = std::upper_bound(ownedRanges.begin(), ownedRanges.end(), addr,
                               [](uint32_t a, const std::pair<uint32_t, uint32_t>& b) {
                                 return a < b.first;
                               });
    return it != ownedRanges.begin() && addr < std::prev(it)->second;
  };

  // Code ranges, so a pointer can be recognised without asking the graph.
  std::vector<std::pair<uint32_t, uint32_t>> codeRanges;
  for (const auto& region : ctx.decoded().codeRegions()) {
    codeRanges.emplace_back(region.start, region.end);
  }
  if (codeRanges.empty()) {
    REXCODEGEN_WARN("dataPointerScan: no code regions, skipping");
    return;
  }
  auto inCode = [&codeRanges](uint32_t addr) {
    for (const auto& [start, end] : codeRanges) {
      if (addr >= start && addr < end) {
        return true;
      }
    }
    return false;
  };

  size_t foundCount = 0;
  for (const auto& section : binary.sections()) {
    if (section.executable || !section.data) {
      continue;
    }
    if (section.name != ".rdata" && section.name != ".data") {
      continue;
    }

    for (uint32_t offset = 0; offset + 4 <= section.size; offset += 4) {
      // Addresses are stored big-endian, like every other 32-bit value here.
      uint32_t value = load_and_swap<uint32_t>(section.data + offset);
      // A function entry point is always 4-byte aligned. Without this the scan
      // picks up unrelated integers that happen to fall in a code range, and
      // each one starts a bogus block decode that runs off into data - which
      // shows up later as an unresolved call to an address no instruction can
      // produce.
      if ((value & 3) != 0) {
        continue;
      }
      if (!inCode(value) || existingFunctions.contains(value)) {
        continue;
      }
      // Code a function already discovered owns is not a new entry point. See
      // the note above.
      if (ownedByAFunction(value)) {
        REXCODEGEN_TRACE("dataPointerScan: 0x{:08X} (pointer at 0x{:08X}) is inside an "
                         "existing function, skipping",
                         value, section.baseAddress + offset);
        continue;
      }
      // The import thunk and export tables live at the end of .text and are not
      // code: a pointer to one is a pointer to a jump table entry, not to a
      // function that needs recompiling.
      if (binary.isInImportExportRange(value)) {
        continue;
      }
      graph.addFunction(value, 4, FunctionAuthority::DISCOVERED, true);
      existingFunctions.insert(value);
      foundCount++;
      REXCODEGEN_TRACE("dataPointerScan: found 0x{:08X} via pointer at 0x{:08X}", value,
                       section.baseAddress + offset);
    }
  }

  REXCODEGEN_TRACE("dataPointerScan: found {} new function pointer targets", foundCount);
  if (foundCount) {
    REXCODEGEN_INFO("dataPointerScan: {} function entry points found in .rdata/.data", foundCount);
  }
}

}  // anonymous namespace

/// Discover blocks for all pending functions (shared helper, declared in phase_helpers.h).
size_t discoverPendingFunctions(CodegenContext& ctx,
                                const std::unordered_set<uint32_t>& knownFunctions) {
  std::vector<uint32_t> pending;
  for (const auto& [addr, node] : ctx.graph.functions()) {
    if (node->canDiscover()) {
      pending.push_back(addr);
    }
  }
  for (uint32_t funcAddr : pending) {
    discoverFunction(ctx, funcAddr, knownFunctions);
  }
  return pending.size();
}

namespace phases {

VoidResult Discover(CodegenContext& ctx, ProgressReporter* reporter) {
  (void)reporter;
  discoverAllFunctions(ctx);
  return Ok();
}

}  // namespace phases

}  // namespace rex::codegen
