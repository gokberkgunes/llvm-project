//===--- HLSL.cpp - HLSL ToolChain Implementations --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HLSL.h"
#include "CommonArgs.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Job.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Triple.h"

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang::driver::toolchains;
using namespace clang;
using namespace llvm::opt;
using namespace llvm;

namespace {

const unsigned OfflineLibMinor = 0xF;

bool isLegalShaderModel(Triple &T) {
  if (T.getOS() != Triple::OSType::ShaderModel)
    return false;

  auto Version = T.getOSVersion();
  if (Version.getBuild())
    return false;
  if (Version.getSubminor())
    return false;

  auto Kind = T.getEnvironment();

  switch (Kind) {
  default:
    return false;
  case Triple::EnvironmentType::Vertex:
  case Triple::EnvironmentType::Hull:
  case Triple::EnvironmentType::Domain:
  case Triple::EnvironmentType::Geometry:
  case Triple::EnvironmentType::Pixel:
  case Triple::EnvironmentType::Compute: {
    VersionTuple MinVer(4, 0);
    return MinVer <= Version;
  } break;
  case Triple::EnvironmentType::Library: {
    VersionTuple SM6x(6, OfflineLibMinor);
    if (Version == SM6x)
      return true;

    VersionTuple MinVer(6, 3);
    return MinVer <= Version;
  } break;
  case Triple::EnvironmentType::Amplification:
  case Triple::EnvironmentType::Mesh: {
    VersionTuple MinVer(6, 5);
    return MinVer <= Version;
  } break;
  }
  return false;
}

std::optional<std::string> tryParseProfile(StringRef Profile) {
  // [ps|vs|gs|hs|ds|cs|ms|as]_[major]_[minor]
  SmallVector<StringRef, 3> Parts;
  Profile.split(Parts, "_");
  if (Parts.size() != 3)
    return std::nullopt;

  Triple::EnvironmentType Kind =
      StringSwitch<Triple::EnvironmentType>(Parts[0])
          .Case("ps", Triple::EnvironmentType::Pixel)
          .Case("vs", Triple::EnvironmentType::Vertex)
          .Case("gs", Triple::EnvironmentType::Geometry)
          .Case("hs", Triple::EnvironmentType::Hull)
          .Case("ds", Triple::EnvironmentType::Domain)
          .Case("cs", Triple::EnvironmentType::Compute)
          .Case("lib", Triple::EnvironmentType::Library)
          .Case("ms", Triple::EnvironmentType::Mesh)
          .Case("as", Triple::EnvironmentType::Amplification)
          .Default(Triple::EnvironmentType::UnknownEnvironment);
  if (Kind == Triple::EnvironmentType::UnknownEnvironment)
    return std::nullopt;

  unsigned long long Major = 0;
  if (llvm::getAsUnsignedInteger(Parts[1], 0, Major))
    return std::nullopt;

  unsigned long long Minor = 0;
  if (Parts[2] == "x" && Kind == Triple::EnvironmentType::Library)
    Minor = OfflineLibMinor;
  else if (llvm::getAsUnsignedInteger(Parts[2], 0, Minor))
    return std::nullopt;

  // dxil-unknown-shadermodel-hull
  llvm::Triple T;
  T.setArch(Triple::ArchType::dxil);
  T.setOSName(Triple::getOSTypeName(Triple::OSType::ShaderModel).str() +
              VersionTuple(Major, Minor).getAsString());
  T.setEnvironment(Kind);
  if (isLegalShaderModel(T))
    return T.getTriple();
  else
    return std::nullopt;
}

bool isLegalValidatorVersion(StringRef ValVersionStr, const Driver &D) {
  VersionTuple Version;
  if (Version.tryParse(ValVersionStr) || Version.getBuild() ||
      Version.getSubminor() || !Version.getMinor()) {
    D.Diag(diag::err_drv_invalid_format_dxil_validator_version)
        << ValVersionStr;
    return false;
  }

  uint64_t Major = Version.getMajor();
  uint64_t Minor = *Version.getMinor();
  if (Major == 0 && Minor != 0) {
    D.Diag(diag::err_drv_invalid_empty_dxil_validator_version) << ValVersionStr;
    return false;
  }
  VersionTuple MinVer(1, 0);
  if (Version < MinVer) {
    D.Diag(diag::err_drv_invalid_range_dxil_validator_version) << ValVersionStr;
    return false;
  }
  return true;
}

} // namespace

void tools::hlsl::Validator::ConstructJob(Compilation &C, const JobAction &JA,
                                          const InputInfo &Output,
                                          const InputInfoList &Inputs,
                                          const ArgList &Args,
                                          const char *LinkingOutput) const {
  std::string DxvPath = getToolChain().GetProgramPath("dxv");
  assert(DxvPath != "dxv" && "cannot find dxv");

  ArgStringList CmdArgs;
  assert(Inputs.size() == 1 && "Unable to handle multiple inputs.");
  const InputInfo &Input = Inputs[0];
  assert(Input.isFilename() && "Unexpected verify input");
  // Grabbing the output of the earlier cc1 run.
  CmdArgs.push_back(Input.getFilename());
  // Use the same name as output.
  CmdArgs.push_back("-o");
  CmdArgs.push_back(Input.getFilename());

  const char *Exec = Args.MakeArgString(DxvPath);
  C.addCommand(std::make_unique<Command>(JA, *this, ResponseFileSupport::None(),
                                         Exec, CmdArgs, Inputs, Input));
}

/// DirectX Toolchain
HLSLToolChain::HLSLToolChain(const Driver &D, const llvm::Triple &Triple,
                             const ArgList &Args)
    : ToolChain(D, Triple, Args) {
  if (Args.hasArg(options::OPT_dxc_validator_path_EQ))
    getProgramPaths().push_back(
        Args.getLastArgValue(options::OPT_dxc_validator_path_EQ).str());
}

Tool *clang::driver::toolchains::HLSLToolChain::getTool(
    Action::ActionClass AC) const {
  switch (AC) {
  case Action::BinaryAnalyzeJobClass:
    if (!Validator)
      Validator.reset(new tools::hlsl::Validator(*this));
    return Validator.get();
  default:
    return ToolChain::getTool(AC);
  }
}

std::optional<std::string>
clang::driver::toolchains::HLSLToolChain::parseTargetProfile(
    StringRef TargetProfile) {
  return tryParseProfile(TargetProfile);
}

DerivedArgList *
HLSLToolChain::TranslateArgs(const DerivedArgList &Args, StringRef BoundArch,
                             Action::OffloadKind DeviceOffloadKind) const {
  DerivedArgList *DAL = new DerivedArgList(Args.getBaseArgs());

  const OptTable &Opts = getDriver().getOpts();

  for (Arg *A : Args) {
    if (A->getOption().getID() == options::OPT_dxil_validator_version) {
      StringRef ValVerStr = A->getValue();
      std::string ErrorMsg;
      if (!isLegalValidatorVersion(ValVerStr, getDriver()))
        continue;
    }
    if (A->getOption().getID() == options::OPT_dxc_entrypoint) {
      DAL->AddSeparateArg(nullptr, Opts.getOption(options::OPT_hlsl_entrypoint),
                          A->getValue());
      A->claim();
      continue;
    }
    if (A->getOption().getID() == options::OPT__SLASH_O) {
      StringRef OStr = A->getValue();
      if (OStr == "d") {
        DAL->AddFlagArg(nullptr, Opts.getOption(options::OPT_O0));
        A->claim();
        continue;
      } else {
        DAL->AddJoinedArg(nullptr, Opts.getOption(options::OPT_O), OStr);
        A->claim();
        continue;
      }
    }
    if (A->getOption().getID() == options::OPT_emit_pristine_llvm) {
      // Translate fcgl into -S -emit-llvm and -disable-llvm-passes.
      DAL->AddFlagArg(nullptr, Opts.getOption(options::OPT_S));
      DAL->AddFlagArg(nullptr, Opts.getOption(options::OPT_emit_llvm));
      DAL->AddFlagArg(nullptr,
                      Opts.getOption(options::OPT_disable_llvm_passes));
      A->claim();
      continue;
    }
    DAL->append(A);
  }

  if (DAL->hasArg(options::OPT_o)) {
    // When run the whole pipeline.
    if (!DAL->hasArg(options::OPT_emit_llvm))
      // Emit obj if write to file.
      DAL->AddFlagArg(nullptr, Opts.getOption(options::OPT_emit_obj));
  } else
    DAL->AddSeparateArg(nullptr, Opts.getOption(options::OPT_o), "-");

  // Add default validator version if not set.
  // TODO: remove this once read validator version from validator.
  if (!DAL->hasArg(options::OPT_dxil_validator_version)) {
    const StringRef DefaultValidatorVer = "1.7";
    DAL->AddSeparateArg(nullptr,
                        Opts.getOption(options::OPT_dxil_validator_version),
                        DefaultValidatorVer);
  }
  if (!DAL->hasArg(options::OPT_O_Group)) {
    DAL->AddJoinedArg(nullptr, Opts.getOption(options::OPT_O), "3");
  }
  // FIXME: add validation for enable_16bit_types should be after HLSL 2018 and
  // shader model 6.2.
  // See: https://github.com/llvm/llvm-project/issues/57876
  return DAL;
}

bool HLSLToolChain::requiresValidation(DerivedArgList &Args) const {
  if (Args.getLastArg(options::OPT_dxc_disable_validation))
    return false;

  std::string DxvPath = GetProgramPath("dxv");
  if (DxvPath != "dxv")
    return true;

  getDriver().Diag(diag::warn_drv_dxc_missing_dxv);
  return false;
}
