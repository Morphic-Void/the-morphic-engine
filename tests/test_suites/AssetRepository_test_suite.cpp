
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    AssetRepository_test_suite.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    12 Aug 26

#include <cstdint>
#include <iostream>
#include <type_traits>
#include <utility>

#include "assets/asset_repository.hpp"
#include "system/transported_types.hpp"
#include "tests/test_suites/AssetRepository_test_suite.hpp"
#include "tests/support/test_context.hpp"

namespace
{

using TTestContext = tests::TTestContext;

CErasedOwner make_test_asset()
{
    return CErasedOwner::create<LoadedFile>();
}

void test_asset_id_contract(TTestContext& ctx)
{
    static_assert(std::is_trivially_copyable_v<CAssetId>);
    static_assert(std::is_standard_layout_v<CAssetId>);
    static_assert(sizeof(CAssetId) == sizeof(std::uint64_t));

    const CAssetId invalid;
    TEST_EXPECT(ctx, !invalid.is_valid());
    TEST_EXPECT(ctx, !static_cast<bool>(invalid));
    TEST_EXPECT(ctx, invalid.query_value() == 0u);
    TEST_EXPECT(ctx, invalid.relationship(CAssetId{}) == 0);
}

void test_repository_identity_and_reuse(TTestContext& ctx)
{
    CAssetRepository repository;
    TEST_EXPECT(ctx, repository.is_valid());
    TEST_EXPECT(ctx, repository.is_empty());
    TEST_EXPECT(ctx, !repository.is_ready());
    TEST_EXPECT(ctx, repository.resolve(CAssetId{}) == nullptr);
    TEST_EXPECT(ctx, !repository.erase(CAssetId{}));
    TEST_EXPECT(ctx, repository.initialise(4u, 4u));

    CErasedOwner first_owner = make_test_asset();
    CErasedOwner second_owner = make_test_asset();
    first_owner.add_hazard(mount_point_ids::conditioning);
    const CAssetId first_id = repository.insert(std::move(first_owner));
    const CAssetId second_id = repository.insert(std::move(second_owner));
    TEST_EXPECT(ctx, repository.has_dependency(mount_point_ids::conditioning));
    TEST_EXPECT(ctx, !repository.has_dependency(mount_point_ids::executive));

    TEST_EXPECT(ctx, first_id.query_value() == 1u);
    TEST_EXPECT(ctx, second_id.query_value() == 2u);
    TEST_EXPECT(ctx, !first_owner);
    TEST_EXPECT(ctx, !second_owner);
    TEST_EXPECT(ctx, repository.resolve(first_id) != nullptr);
    TEST_EXPECT(ctx, repository.resolve(second_id) != nullptr);
    TEST_EXPECT(ctx, repository.resolve(first_id)->query_type_id() ==
        type_id{ k_system_type_id_v<LoadedFile> });
    TEST_EXPECT(ctx, repository.resolve(first_id)->payload<LoadedFile>() != nullptr);

    CAssetRecord* const second_record = repository.resolve(second_id);
    TEST_EXPECT(ctx, repository.erase(first_id));
    TEST_EXPECT(ctx, !repository.has_dependency(mount_point_ids::conditioning));
    TEST_EXPECT(ctx, repository.resolve(first_id) == nullptr);

    CErasedOwner third_owner = make_test_asset();
    const CAssetId third_id = repository.insert(std::move(third_owner));
    TEST_EXPECT(ctx, third_id.query_value() == 3u);
    TEST_EXPECT(ctx, third_id != first_id);
    TEST_EXPECT(ctx, repository.resolve(first_id) == nullptr);

    repository.compact();
    TEST_EXPECT(ctx, repository.resolve(second_id) == second_record);
    TEST_EXPECT(ctx, repository.resolve(third_id) != nullptr);
    TEST_EXPECT(ctx, repository.check_integrity());
}

void test_repository_reinitialisation_does_not_reuse_ids(TTestContext& ctx)
{
    CAssetRepository repository;
    TEST_EXPECT(ctx, repository.initialise());

    CErasedOwner first_owner = make_test_asset();
    const CAssetId stale_id = repository.insert(std::move(first_owner));
    TEST_EXPECT(ctx, stale_id.query_value() == 1u);

    TEST_EXPECT(ctx, repository.initialise());
    TEST_EXPECT(ctx, repository.resolve(stale_id) == nullptr);

    CErasedOwner second_owner = make_test_asset();
    const CAssetId current_id = repository.insert(std::move(second_owner));
    TEST_EXPECT(ctx, current_id.query_value() == 2u);
    TEST_EXPECT(ctx, repository.resolve(stale_id) == nullptr);
    TEST_EXPECT(ctx, repository.resolve(current_id) != nullptr);

    CErasedOwner empty_owner;
    TEST_EXPECT(ctx, !repository.insert(std::move(empty_owner)));
    TEST_EXPECT(ctx, empty_owner.is_empty());
}

void test_repository_dependency_disposal(TTestContext& ctx)
{
    CAssetRepository repository;
    TEST_EXPECT(ctx, repository.initialise(4u, 4u));
    CAssetId ids[6]{};
    for (std::uint32_t index = 0u; index < 6u; ++index)
    {
        CErasedOwner owner = make_test_asset();
        owner.add_hazard((index == 2u) ? mount_point_ids::executive : mount_point_ids::conditioning);
        ids[index] = repository.insert(std::move(owner));
        TEST_EXPECT(ctx, ids[index].is_valid());
    }
    TEST_EXPECT(ctx, repository.erase(ids[1]));
    CAssetRecord* const survivor = repository.resolve(ids[2]);
    repository.erase_dependencies(mount_point_ids::conditioning);
    TEST_EXPECT(ctx, !repository.has_dependency(mount_point_ids::conditioning));
    TEST_EXPECT(ctx, repository.has_dependency(mount_point_ids::executive));
    for (std::uint32_t index = 0u; index < 6u; ++index)
    {
        TEST_EXPECT(ctx, repository.resolve(ids[index]) == ((index == 2u) ? survivor : nullptr));
    }
    TEST_EXPECT(ctx, repository.check_integrity());
    repository.erase_dependencies(mount_point_ids::conditioning);
    repository.erase_dependencies(mount_point_ids::executive);
    TEST_EXPECT(ctx, repository.is_empty());
    TEST_EXPECT(ctx, repository.check_integrity());
}

}   //  namespace

int run_asset_repository_tests()
{
    TTestContext ctx;
    test_asset_id_contract(ctx);
    test_repository_identity_and_reuse(ctx);
    test_repository_reinitialisation_does_not_reuse_ids(ctx);
    test_repository_dependency_disposal(ctx);

    std::cout << "AssetRepository: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    return ctx.failed;
}
