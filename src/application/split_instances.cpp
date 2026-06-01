#include "autobbox/application/split_instances.h"

#include "autobbox/common/files.h"
#include "autobbox/common/strings.h"
#include "autobbox/creo/model_info.h"

#include <ProArray.h>
#include <ProAsmcomp.h>
#include <ProAsmcomppath.h>
#include <ProFaminstance.h>
#include <ProFeature.h>
#include <ProMdl.h>
#include <ProSolid.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <string>
#include <utility>
#include <vector>

namespace autobbox::application {

namespace {

void LogLine(const SplitLogSink &log_sink, const char *fmt, ...)
{
    if (!log_sink || fmt == nullptr) {
        return;
    }

    char buffer[4096] = {0};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    log_sink(buffer);
}

template <size_t N>
void CopyWStr(wchar_t (&dest)[N], const wchar_t *src)
{
    if (N == 0) {
        return;
    }

    size_t i = 0;
    if (src != nullptr) {
        while (i + 1 < N && src[i] != L'\0') {
            dest[i] = src[i];
            ++i;
        }
    }
    dest[i] = L'\0';
}

std::string DefaultModelTag(ProMdl mdl)
{
    return autobbox::creo::DefaultModelTag(mdl);
}

std::string ModelTag(ProMdl mdl, const SplitModelTagFormatter &format_model_tag)
{
    if (format_model_tag) {
        return format_model_tag(mdl);
    }
    return DefaultModelTag(mdl);
}

std::string SanitizeAsciiToken(const std::wstring &value, size_t max_len)
{
    std::string raw = autobbox::common::WToA(value.c_str());
    std::string out;
    out.reserve(raw.size());
    for (unsigned char ch : raw) {
        if (std::isalnum(ch)) {
            out.push_back(static_cast<char>(std::toupper(ch)));
        } else if (ch == '_' || ch == '-') {
            out.push_back('_');
        }
    }
    if (out.empty()) {
        out = "MODEL";
    }
    if (out.size() > max_len) {
        out.resize(max_len);
    }
    return out;
}

bool IsFamilyInstanceQuick(ProMdl mdl)
{
    ProMdl generic = nullptr;
    ProError status = ProFaminstanceGenericGet(mdl, PRO_B_TRUE, &generic);
    if (status == PRO_TK_NO_ERROR) {
        if (generic == mdl) {
            return false;
        }
        if (generic != nullptr &&
            autobbox::creo::ModelName(generic, L"") == autobbox::creo::ModelName(mdl, L"")) {
            return false;
        }
        return true;
    }

    ProName gen_name = {0};
    ProMdlType gen_type = PRO_MDL_UNUSED;
    status = ProFaminstanceImmediategenericinfoGet(mdl, gen_name, &gen_type);
    return status == PRO_TK_NO_ERROR && gen_type != PRO_MDL_UNUSED;
}

std::wstring BuildSplitOutputDir(bool output_to_split_dir)
{
    const std::wstring cwd = autobbox::common::CurrentWorkingDirectoryW();
    if (cwd.empty()) {
        return std::wstring();
    }
    if (!output_to_split_dir) {
        return cwd;
    }
    return autobbox::common::JoinPath(cwd, L"AB_SPLIT");
}

std::wstring MakeSplitBaseName(const std::wstring &model_name)
{
    const std::string core = SanitizeAsciiToken(model_name, 22);
    return autobbox::common::AToW((core + "_SPLIT").c_str());
}

bool SplitNameExists(const std::wstring &dir, const std::wstring &name, ProMdlType type)
{
    ProMdl existing = nullptr;
    ProMdlName pro_name = {0};
    CopyWStr(pro_name, name.c_str());
    if (ProMdlnameInit(pro_name, autobbox::creo::ToMdlFileType(type), &existing) == PRO_TK_NO_ERROR &&
        existing != nullptr) {
        return true;
    }

    const wchar_t *ext = nullptr;
    if (!autobbox::creo::MdlTypeToExt(type, &ext)) {
        return false;
    }
    std::wstring full = autobbox::common::JoinPath(dir, name.c_str());
    full += ext;
    return autobbox::common::FileExistsW(full);
}

bool ResolveSplitModelName(const core::SplitCandidate &cand,
                           const std::wstring &dir,
                           bool reuse_existing,
                           std::wstring &name_out,
                           bool &reused_out)
{
    name_out.clear();
    reused_out = false;
    const std::wstring base = MakeSplitBaseName(cand.model_name);
    for (int i = 0; i < 1000; ++i) {
        std::wstring name = base;
        if (i > 0) {
            wchar_t suffix[16] = {0};
            std::swprintf(suffix, sizeof(suffix) / sizeof(suffix[0]), L"_%02d", i);
            const size_t max_core = 31 - std::wcslen(suffix);
            if (name.size() > max_core) {
                name.resize(max_core);
            }
            name += suffix;
        }
        if (name.size() > 31) {
            name.resize(31);
        }
        const bool exists = SplitNameExists(dir, name, cand.type);
        if (exists) {
            if (reuse_existing) {
                name_out = name;
                reused_out = true;
                return true;
            }
            continue;
        }
        name_out = name;
        reused_out = false;
        return true;
    }
    return false;
}

ProError LoadExistingSplitModel(const std::wstring &dir,
                                const std::wstring &name,
                                ProMdlType type,
                                ProMdl *mdl_out,
                                bool &independent_out)
{
    if (mdl_out != nullptr) {
        *mdl_out = nullptr;
    }
    independent_out = false;
    if (dir.empty() || name.empty()) {
        return PRO_TK_BAD_INPUTS;
    }

    ProMdl mdl = nullptr;
    ProMdlName pro_name = {0};
    CopyWStr(pro_name, name.c_str());
    ProError status = ProMdlnameInit(pro_name, autobbox::creo::ToMdlFileType(type), &mdl);
    if (status != PRO_TK_NO_ERROR || mdl == nullptr) {
        const wchar_t *ext = nullptr;
        if (!autobbox::creo::MdlTypeToExt(type, &ext)) {
            return PRO_TK_BAD_INPUTS;
        }
        std::wstring full = autobbox::common::JoinPath(dir, name.c_str());
        full += ext;
        if (!autobbox::common::FileExistsW(full)) {
            return PRO_TK_E_NOT_FOUND;
        }
        ProPath full_path = {0};
        CopyWStr(full_path, full.c_str());
        status = ProMdlFiletypeLoad(full_path, PRO_MDLFILE_UNUSED, PRO_B_FALSE, &mdl);
    }
    if (status != PRO_TK_NO_ERROR || mdl == nullptr) {
        return status == PRO_TK_NO_ERROR ? PRO_TK_GENERAL_ERROR : status;
    }

    independent_out = !IsFamilyInstanceQuick(mdl);
    if (mdl_out != nullptr) {
        *mdl_out = mdl;
    }
    return PRO_TK_NO_ERROR;
}

ProError InitAsmcompFromCandidate(const core::SplitCandidate &cand, ProAsmcomp *comp)
{
    if (comp == nullptr || !cand.has_owner || cand.owner == nullptr || cand.feat_id < 0) {
        return PRO_TK_BAD_INPUTS;
    }
    ProFeature feat;
    const ProError status = ProFeatureInit(cand.owner, cand.feat_id, &feat);
    if (status != PRO_TK_NO_ERROR) {
        return status;
    }
    std::memcpy(comp, &feat, sizeof(ProAsmcomp));
    return PRO_TK_NO_ERROR;
}

ProAsmcomppath MakeRootOwnerPath(ProSolid owner)
{
    ProAsmcomppath path;
    std::memset(&path, 0, sizeof(path));
    if (owner != nullptr) {
        ProIdTable ids = {0};
        ProAsmcomppathInit(owner, ids, 0, &path);
    }
    return path;
}

ProError DeleteFeatureById(ProSolid owner, int feat_id)
{
    if (owner == nullptr || feat_id < 0) {
        return PRO_TK_BAD_INPUTS;
    }
    return ProFeatureDelete(owner, &feat_id, 1, nullptr, 0);
}

ProError BackupModelToDirectory(ProMdl mdl, const std::wstring &dir)
{
    if (mdl == nullptr || dir.empty()) {
        return PRO_TK_BAD_INPUTS;
    }
    ProPath out_dir = {0};
    CopyWStr(out_dir, dir.c_str());
    return ProMdlnameBackup(mdl, out_dir);
}

void SetIdentityMatrix(ProMatrix matrix)
{
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            matrix[r][c] = (r == c) ? 1.0 : 0.0;
        }
    }
}

ProError ReplaceComponentWithModel(const core::SplitCandidate &cand,
                                   ProMdl replacement_mdl,
                                   const SplitModelTagFormatter &format_model_tag,
                                   const SplitLogSink &log_sink,
                                   bool &replaced_out)
{
    const ULONGLONG total_begin = GetTickCount64();
    replaced_out = false;
    if (!cand.has_owner || cand.owner == nullptr || replacement_mdl == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }

    ProAsmcomp old_comp;
    ProError status = InitAsmcompFromCandidate(cand, &old_comp);
    LogLine(log_sink,
            "split-assemble-replace-init-comp %s feat_id=%d status=%d",
            ModelTag(cand.mdl, format_model_tag).c_str(),
            cand.feat_id,
            static_cast<int>(status));
    if (status != PRO_TK_NO_ERROR) {
        return status;
    }

    ProAsmcomppath owner_path = MakeRootOwnerPath(cand.owner);

    ProMatrix old_pos = {{0}};
    bool have_pos = false;
    const ULONGLONG pos_begin = GetTickCount64();
    status = ProAsmcompPositionGet(&old_comp, old_pos);
    const ULONGLONG pos_ms = GetTickCount64() - pos_begin;
    if (status == PRO_TK_NO_ERROR) {
        have_pos = true;
    } else {
        SetIdentityMatrix(old_pos);
    }
    LogLine(log_sink,
            "split-assemble-replace-position-get %s status=%d have_pos=%d",
            ModelTag(cand.mdl, format_model_tag).c_str(),
            static_cast<int>(status),
            have_pos ? 1 : 0);
    LogLine(log_sink,
            "split-perf replace-position-get %s elapsed_ms=%llu",
            ModelTag(cand.mdl, format_model_tag).c_str(),
            static_cast<unsigned long long>(pos_ms));

    ProAsmcompconstraint *constraints = nullptr;
    const ULONGLONG constraints_begin = GetTickCount64();
    const ProError constraints_status =
        ProAsmcompConstraintsWithComppathGet(&old_comp, &owner_path, &constraints);
    const ULONGLONG constraints_ms = GetTickCount64() - constraints_begin;
    if (constraints_status != PRO_TK_NO_ERROR) {
        constraints = nullptr;
    }
    LogLine(log_sink,
            "split-assemble-replace-constraints-get %s status=%d has_constraints=%d",
            ModelTag(cand.mdl, format_model_tag).c_str(),
            static_cast<int>(constraints_status),
            constraints != nullptr ? 1 : 0);
    LogLine(log_sink,
            "split-perf replace-constraints-get %s elapsed_ms=%llu",
            ModelTag(cand.mdl, format_model_tag).c_str(),
            static_cast<unsigned long long>(constraints_ms));

    ProAsmcomp new_comp;
    std::memset(&new_comp, 0, sizeof(new_comp));
    const ULONGLONG assemble_begin = GetTickCount64();
    status = ProAsmcompAssemble(reinterpret_cast<ProAssembly>(cand.owner),
                                reinterpret_cast<ProSolid>(replacement_mdl),
                                old_pos,
                                &new_comp);
    const ULONGLONG assemble_ms = GetTickCount64() - assemble_begin;
    LogLine(log_sink,
            "split-assemble-replace-assemble %s repl=%s status=%d",
            ModelTag(cand.mdl, format_model_tag).c_str(),
            ModelTag(replacement_mdl, format_model_tag).c_str(),
            static_cast<int>(status));
    LogLine(log_sink,
            "split-perf replace-assemble %s repl=%s elapsed_ms=%llu",
            ModelTag(cand.mdl, format_model_tag).c_str(),
            ModelTag(replacement_mdl, format_model_tag).c_str(),
            static_cast<unsigned long long>(assemble_ms));
    if (status != PRO_TK_NO_ERROR) {
        if (constraints != nullptr) {
            ProArrayFree(reinterpret_cast<ProArray *>(&constraints));
        }
        return status;
    }

    const int new_feat_id = new_comp.id;
    auto rollback_new_component = [&]() {
        if (new_feat_id >= 0) {
            DeleteFeatureById(cand.owner, new_feat_id);
        }
    };

    if (constraints != nullptr) {
        const ULONGLONG constraints_set_begin = GetTickCount64();
        const ProError set_status = ProAsmcompConstraintsSet(&owner_path, &new_comp, constraints);
        const ULONGLONG constraints_set_ms = GetTickCount64() - constraints_set_begin;
        ProArrayFree(reinterpret_cast<ProArray *>(&constraints));
        constraints = nullptr;
        LogLine(log_sink,
                "split-assemble-replace-constraints-set %s repl=%s status=%d",
                ModelTag(cand.mdl, format_model_tag).c_str(),
                ModelTag(replacement_mdl, format_model_tag).c_str(),
                static_cast<int>(set_status));
        LogLine(log_sink,
                "split-perf replace-constraints-set %s repl=%s elapsed_ms=%llu",
                ModelTag(cand.mdl, format_model_tag).c_str(),
                ModelTag(replacement_mdl, format_model_tag).c_str(),
                static_cast<unsigned long long>(constraints_set_ms));
        if (set_status != PRO_TK_NO_ERROR) {
            rollback_new_component();
            return set_status;
        }
    }

    const ULONGLONG delete_begin = GetTickCount64();
    status = DeleteFeatureById(cand.owner, cand.feat_id);
    const ULONGLONG delete_ms = GetTickCount64() - delete_begin;
    LogLine(log_sink,
            "split-assemble-replace-delete-old %s feat_id=%d status=%d",
            ModelTag(cand.mdl, format_model_tag).c_str(),
            cand.feat_id,
            static_cast<int>(status));
    LogLine(log_sink,
            "split-perf replace-delete-old %s feat_id=%d elapsed_ms=%llu",
            ModelTag(cand.mdl, format_model_tag).c_str(),
            cand.feat_id,
            static_cast<unsigned long long>(delete_ms));
    if (status != PRO_TK_NO_ERROR) {
        rollback_new_component();
        return status;
    }

    replaced_out = true;
    const ULONGLONG total_ms = GetTickCount64() - total_begin;
    LogLine(log_sink,
            "split-perf replace-total %s elapsed_ms=%llu",
            ModelTag(cand.mdl, format_model_tag).c_str(),
            static_cast<unsigned long long>(total_ms));
    return PRO_TK_NO_ERROR;
}

ProError CopyInstanceStandalone(const core::SplitCandidate &cand,
                                const std::wstring &new_name,
                                ProMdl *new_mdl_out,
                                const SplitModelTagFormatter &format_model_tag,
                                const SplitLogSink &log_sink,
                                bool &independent_out)
{
    const ULONGLONG total_begin = GetTickCount64();
    if (new_mdl_out != nullptr) {
        *new_mdl_out = nullptr;
    }
    independent_out = false;

    ProMdlName pro_name = {0};
    CopyWStr(pro_name, new_name.c_str());
    ProMdl new_mdl = nullptr;
    const ULONGLONG copy_begin = GetTickCount64();
    ProError status = ProMdlnameCopy(cand.mdl, pro_name, &new_mdl);
    const ULONGLONG copy_ms = GetTickCount64() - copy_begin;
    LogLine(log_sink,
            "split-copy-standalone %s new=%s status=%d",
            ModelTag(cand.mdl, format_model_tag).c_str(),
            autobbox::common::WToA(new_name.c_str()).c_str(),
            static_cast<int>(status));
    LogLine(log_sink,
            "split-perf copy-standalone %s new=%s elapsed_ms=%llu",
            ModelTag(cand.mdl, format_model_tag).c_str(),
            autobbox::common::WToA(new_name.c_str()).c_str(),
            static_cast<unsigned long long>(copy_ms));
    if (status != PRO_TK_NO_ERROR || new_mdl == nullptr) {
        return status == PRO_TK_NO_ERROR ? PRO_TK_GENERAL_ERROR : status;
    }

    const ULONGLONG check_begin = GetTickCount64();
    independent_out = !IsFamilyInstanceQuick(new_mdl);
    const ULONGLONG check_ms = GetTickCount64() - check_begin;
    LogLine(log_sink,
            "split-copy-standalone-check %s new=%s independent=%d",
            ModelTag(cand.mdl, format_model_tag).c_str(),
            autobbox::common::WToA(new_name.c_str()).c_str(),
            independent_out ? 1 : 0);
    LogLine(log_sink,
            "split-perf copy-standalone-check %s new=%s elapsed_ms=%llu",
            ModelTag(cand.mdl, format_model_tag).c_str(),
            autobbox::common::WToA(new_name.c_str()).c_str(),
            static_cast<unsigned long long>(check_ms));

    if (new_mdl_out != nullptr) {
        *new_mdl_out = new_mdl;
    }
    const ULONGLONG total_ms = GetTickCount64() - total_begin;
    LogLine(log_sink,
            "split-perf copy-standalone-total %s new=%s elapsed_ms=%llu",
            ModelTag(cand.mdl, format_model_tag).c_str(),
            autobbox::common::WToA(new_name.c_str()).c_str(),
            static_cast<unsigned long long>(total_ms));
    return PRO_TK_NO_ERROR;
}

} // namespace

void ExecuteSplitInstancesTask(int candidates_total,
                               const std::vector<core::SplitCandidate> &selected,
                               const core::SplitRunOptions &options,
                               const SplitModelTagFormatter &format_model_tag,
                               const SplitLogSink &log_sink)
{
    const std::wstring out_dir = BuildSplitOutputDir(options.output_to_split_dir);
    if (out_dir.empty()) {
        LogLine(log_sink, "FAIL split reason=resolve-output-dir");
        return;
    }
    if (options.output_to_split_dir && !autobbox::common::EnsureDirectoryW(out_dir)) {
        LogLine(log_sink,
                "FAIL split reason=create-output-dir path=%s",
                autobbox::common::WToA(out_dir.c_str()).c_str());
        return;
    }

    LogLine(log_sink,
            "split selected=%d replace=%d reuse_existing=%d out_dir=%s",
            static_cast<int>(selected.size()),
            options.replace_in_assembly ? 1 : 0,
            options.reuse_existing_split ? 1 : 0,
            autobbox::common::WToA(out_dir.c_str()).c_str());

    int ok_count = 0;
    int fail_count = 0;
    int backup_ok_count = 0;
    int backup_fail_count = 0;
    const ULONGLONG split_begin_ms = GetTickCount64();
    std::vector<std::pair<ProMdl, std::wstring>> pending_backups;
    for (const core::SplitCandidate &cand : selected) {
        const ULONGLONG item_begin_ms = GetTickCount64();
        std::wstring new_name;
        bool reused_existing = false;
        if (!ResolveSplitModelName(cand, out_dir, options.reuse_existing_split, new_name, reused_existing) ||
            new_name.empty()) {
            ++fail_count;
            LogLine(log_sink, "FAIL %s reason=alloc-name", ModelTag(cand.mdl, format_model_tag).c_str());
            continue;
        }

        ProMdl new_mdl = nullptr;
        bool independent = false;
        bool replaced = false;
        ProError status = PRO_TK_GENERAL_ERROR;

        if (reused_existing) {
            const ULONGLONG reuse_begin = GetTickCount64();
            status = LoadExistingSplitModel(out_dir, new_name, cand.type, &new_mdl, independent);
            const ULONGLONG reuse_ms = GetTickCount64() - reuse_begin;
            LogLine(log_sink,
                    "split-reuse-existing %s use=%s status=%d independent=%d elapsed_ms=%llu",
                    ModelTag(cand.mdl, format_model_tag).c_str(),
                    autobbox::common::WToA(new_name.c_str()).c_str(),
                    static_cast<int>(status),
                    independent ? 1 : 0,
                    static_cast<unsigned long long>(reuse_ms));
            if (status != PRO_TK_NO_ERROR || new_mdl == nullptr || !independent) {
                LogLine(log_sink,
                        "split-reuse-existing %s fallback=create-new status=%d independent=%d",
                        ModelTag(cand.mdl, format_model_tag).c_str(),
                        static_cast<int>(status),
                        independent ? 1 : 0);
                reused_existing = false;
                if (!ResolveSplitModelName(cand, out_dir, false, new_name, reused_existing) || new_name.empty()) {
                    ++fail_count;
                    LogLine(log_sink,
                            "FAIL %s reason=alloc-name-after-reuse",
                            ModelTag(cand.mdl, format_model_tag).c_str());
                    continue;
                }
                new_mdl = nullptr;
                independent = false;
                status = PRO_TK_GENERAL_ERROR;
            }
        }

        if (!reused_existing) {
            status = CopyInstanceStandalone(
                cand,
                new_name,
                &new_mdl,
                format_model_tag,
                log_sink,
                independent);
            if (options.replace_in_assembly && cand.has_owner && status != PRO_TK_NO_ERROR) {
                LogLine(log_sink,
                        "split-replace-direct-copy %s status=%d independent=%d",
                        ModelTag(cand.mdl, format_model_tag).c_str(),
                        static_cast<int>(status),
                        independent ? 1 : 0);
            }
        } else {
            status = PRO_TK_NO_ERROR;
        }

        if (status == PRO_TK_NO_ERROR && independent && new_mdl != nullptr &&
            options.replace_in_assembly && cand.has_owner && !replaced) {
            bool assembled_replaced = false;
            const ProError replace_status = ReplaceComponentWithModel(
                cand,
                new_mdl,
                format_model_tag,
                log_sink,
                assembled_replaced);
            LogLine(log_sink,
                    "split-replace-direct-assemble %s status=%d replaced=%d reused=%d",
                    ModelTag(cand.mdl, format_model_tag).c_str(),
                    static_cast<int>(replace_status),
                    assembled_replaced ? 1 : 0,
                    reused_existing ? 1 : 0);
            if (replace_status == PRO_TK_NO_ERROR && assembled_replaced) {
                replaced = true;
            } else {
                status = replace_status;
            }
        }

        if (status == PRO_TK_NO_ERROR && independent) {
            ++ok_count;
            if (options.output_to_split_dir && new_mdl != nullptr && !reused_existing) {
                pending_backups.emplace_back(new_mdl, new_name);
            }
            LogLine(log_sink,
                    "OK   %s split new=%s generic=%s replace=%d reused=%d out_dir=%s",
                    ModelTag(cand.mdl, format_model_tag).c_str(),
                    autobbox::common::WToA(new_name.c_str()).c_str(),
                    autobbox::common::WToA(cand.generic_name.c_str()).c_str(),
                    replaced ? 1 : 0,
                    reused_existing ? 1 : 0,
                    autobbox::common::WToA(out_dir.c_str()).c_str());
        } else {
            ++fail_count;
            LogLine(log_sink,
                    "FAIL %s reason=split-instance status=%d new=%s generic=%s replace=%d reused=%d independent=%d",
                    ModelTag(cand.mdl, format_model_tag).c_str(),
                    static_cast<int>(status),
                    autobbox::common::WToA(new_name.c_str()).c_str(),
                    autobbox::common::WToA(cand.generic_name.c_str()).c_str(),
                    (options.replace_in_assembly && cand.has_owner) ? 1 : 0,
                    reused_existing ? 1 : 0,
                    independent ? 1 : 0);
        }

        const ULONGLONG item_elapsed_ms = GetTickCount64() - item_begin_ms;
        LogLine(log_sink,
                "split-perf item=%s elapsed_ms=%llu replace=%d reused=%d independent=%d",
                ModelTag(cand.mdl, format_model_tag).c_str(),
                static_cast<unsigned long long>(item_elapsed_ms),
                replaced ? 1 : 0,
                reused_existing ? 1 : 0,
                independent ? 1 : 0);
    }

    if (options.output_to_split_dir && !pending_backups.empty()) {
        const ULONGLONG backup_begin_ms = GetTickCount64();
        for (const auto &entry : pending_backups) {
            const ULONGLONG item_backup_begin_ms = GetTickCount64();
            const ProError backup_status = BackupModelToDirectory(entry.first, out_dir);
            const ULONGLONG item_backup_elapsed_ms = GetTickCount64() - item_backup_begin_ms;
            if (backup_status == PRO_TK_NO_ERROR) {
                ++backup_ok_count;
            } else {
                ++backup_fail_count;
            }
            LogLine(log_sink,
                    "split-backup-deferred %s dir=%s status=%d elapsed_ms=%llu",
                    autobbox::common::WToA(entry.second.c_str()).c_str(),
                    autobbox::common::WToA(out_dir.c_str()).c_str(),
                    static_cast<int>(backup_status),
                    static_cast<unsigned long long>(item_backup_elapsed_ms));
        }
        const ULONGLONG backup_elapsed_ms = GetTickCount64() - backup_begin_ms;
        LogLine(log_sink,
                "split-backup-summary total_elapsed_ms=%llu ok=%d fail=%d",
                static_cast<unsigned long long>(backup_elapsed_ms),
                backup_ok_count,
                backup_fail_count);
    }

    LogLine(log_sink,
            "Summary mode=split candidates=%d selected=%d ok=%d fail=%d replace=%d out_dir=%s",
            candidates_total,
            static_cast<int>(selected.size()),
            ok_count,
            fail_count,
            options.replace_in_assembly ? 1 : 0,
            autobbox::common::WToA(out_dir.c_str()).c_str());
    const ULONGLONG split_elapsed_ms = GetTickCount64() - split_begin_ms;
    LogLine(log_sink,
            "split-perf total_elapsed_ms=%llu selected=%d ok=%d fail=%d",
            static_cast<unsigned long long>(split_elapsed_ms),
            static_cast<int>(selected.size()),
            ok_count,
            fail_count);
}

} // namespace autobbox::application
