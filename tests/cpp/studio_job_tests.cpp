#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <thread>
#include <utility>

#include "irop/error.hpp"
#include "irop/geometry/mesh_geometry.hpp"
#include "irop/geometry/transform.hpp"
#include "irop/io/stl_io.hpp"
#include "job.hpp"
#include "support/test_support.hpp"

namespace {

using Catch::Approx;
using irop::studio::Job;
using irop::studio::JobCompletion;
using irop::studio::JobKind;
using irop::studio::JobRequest;

[[nodiscard]] JobCompletion wait_for_completion(Job& job)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto completed = job.take_completion()) {
            return std::move(*completed);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    job.cancel();
    FAIL("Studio background job did not finish within ten seconds");
}

[[nodiscard]] JobRequest write_request(const std::filesystem::path& directory, const JobKind kind)
{
    JobRequest request;
    request.kind = kind;
    request.object_path = directory / std::filesystem::path(u8"source-tętra-网.stl");
    request.container_path = directory / "container.stl";
    request.output_directory = directory / std::filesystem::path(u8"packed-tętra-网");
    request.options.initialization.object_count = 1;
    request.options.initialization.initial_volume_scale = 0.1;
    request.options.initialization.seed = 123;
    request.options.algorithm.final_volume_scale = 0.1001;
    request.options.algorithm.scale_step_count = 1;
    request.options.algorithm.max_iterations_per_scale_step = 3;
    request.options.algorithm.maximum_rotation_delta_radians = 0.01;
    request.options.algorithm.adaptive_sampling = false;
    request.options.limits.max_elapsed_time = std::chrono::seconds(3);
    irop::write_stl(request.object_path, irop::test::tetrahedron_mesh());
    irop::write_stl(request.container_path, irop::test::cube_mesh(5.0));
    return request;
}

TEST_CASE("Studio previews a centered volume-scaled source without moving the container", "[studio][job]")
{
    irop::test::TempDirectory temporary;
    JobRequest request = write_request(temporary.path(), JobKind::preview);
    // Replace the fixture inputs so their original world pivots cannot pass
    // accidentally if preview omits centering or moves the container as well.
    REQUIRE(std::filesystem::remove(request.object_path));
    REQUIRE(std::filesystem::remove(request.container_path));
    irop::write_stl(request.object_path,
                    irop::transform_mesh(irop::test::tetrahedron_mesh(), irop::Transform {
                                                                             .translation = { 10.0, 20.0, -3.0 }
    }));
    irop::write_stl(request.container_path,
                    irop::transform_mesh(irop::test::cube_mesh(5.0), irop::Transform {
                                                                         .translation = { 2.0, -1.0, 3.0 }
    }));
    request.options.initialization.initial_volume_scale = 0.125;
    request.options.initialization.object_count = 36;

    Job job;
    CHECK_FALSE(job.active());
    CHECK_FALSE(job.progress());
    CHECK_FALSE(job.take_completion());
    job.start(request);
    CHECK(job.active());
    const JobCompletion completed = wait_for_completion(job);

    INFO(completed.diagnostic);
    REQUIRE(completed.scene);
    CHECK(completed.diagnostic.empty());
    CHECK_FALSE(completed.cancelled);
    CHECK_FALSE(job.active());
    CHECK_FALSE(job.take_completion());
    const auto& scene = *completed.scene;
    CHECK(scene.command == "preview");
    CHECK(scene.status == "input_preview");
    CHECK_FALSE(scene.success);
    CHECK(scene.object_count == 1);
    CHECK(scene.initial_volume_scale == 0.125);
    CHECK_FALSE(scene.recorded_physical_validity);
    REQUIRE(scene.objects);
    REQUIRE(scene.container);
    CHECK(scene.objects->triangles.size() == 4);
    CHECK(scene.container->triangles.size() == 12);

    const auto centroid = irop::vertex_centroid(*scene.objects);
    CHECK(centroid.x == Approx(0.0).margin(1.0e-12));
    CHECK(centroid.y == Approx(0.0).margin(1.0e-12));
    CHECK(centroid.z == Approx(0.0).margin(1.0e-12));
    const irop::ClosedMeshQuery object(*scene.objects);
    CHECK(object.volume() == Approx(0.125 / 6.0).epsilon(1.0e-12));
    CHECK(object.bounds().minimum.x == Approx(-0.125).margin(1.0e-12));
    CHECK(object.bounds().maximum.x == Approx(0.375).margin(1.0e-12));
    const irop::ClosedMeshQuery container(*scene.container);
    CHECK(container.bounds().minimum.x == -3.0);
    CHECK(container.bounds().maximum.x == 7.0);
    CHECK(container.bounds().minimum.y == -6.0);
    CHECK(container.bounds().maximum.y == 4.0);
    CHECK(container.bounds().minimum.z == -2.0);
    CHECK(container.bounds().maximum.z == 8.0);
    CHECK_FALSE(std::filesystem::exists(request.output_directory));
}

TEST_CASE("Studio preserves published growth results when cancellation arrives after commit", "[studio][job]")
{
    irop::test::TempDirectory temporary;
    const JobRequest request = write_request(temporary.path(), JobKind::pack);
    const auto summary_path = request.output_directory / "run-summary.json";
    Job job;
    job.start(request);

    // The directory is committed atomically. Waiting for its summary makes
    // this a deterministic late stop without relying on worker scheduling.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!std::filesystem::is_regular_file(summary_path) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    job.cancel();
    const JobCompletion completed = wait_for_completion(job);
    INFO(completed.diagnostic);
    REQUIRE(std::filesystem::is_regular_file(summary_path));
    REQUIRE(completed.scene);
    INFO(completed.scene->diagnostic);
    REQUIRE(completed.scene->success);
    CHECK_FALSE(completed.cancelled);
    CHECK(completed.diagnostic.empty());
    CHECK_FALSE(job.active());
    CHECK(completed.scene->command == "pack");
    CHECK(completed.scene->status == "success");
    CHECK(completed.scene->recorded_physical_validity == true);
    CHECK(completed.scene->object_count == 1);
    REQUIRE(completed.scene->objects);
    REQUIRE(completed.scene->container);
    REQUIRE(completed.scene->minimum_volume_scale);
    CHECK(*completed.scene->minimum_volume_scale == Approx(0.1001).epsilon(1.0e-12));
    CHECK(std::filesystem::is_regular_file(request.output_directory / "packed-objects.stl"));
    CHECK(std::filesystem::is_regular_file(request.output_directory / "placements.json"));

    const auto progress = job.progress();
    REQUIRE(progress);
    CHECK(progress->phase == irop::PackingProgressPhase::finished);
    CHECK(progress->objects_at_target == 1);
    std::ifstream input(summary_path, std::ios::binary);
    REQUIRE(input.good());
    nlohmann::json summary;
    input >> summary;
    CHECK(summary.at("work").at("local_solves").get<unsigned long long>() > 0);
    CHECK(summary.at("work").at("tetrahedralization_attempts").get<unsigned long long>() > 0);

    // The same worker owner can open a saved result after delivering its first
    // completion, and the previous run's progress must not leak into that job.
    JobRequest reopen;
    reopen.kind = JobKind::open;
    reopen.summary_path = summary_path;
    job.start(std::move(reopen));
    CHECK(job.active());
    CHECK_FALSE(job.progress());
    const JobCompletion reopened = wait_for_completion(job);
    REQUIRE(reopened.scene);
    CHECK(reopened.scene->success);
    CHECK(reopened.scene->objects->triangles.size() == completed.scene->objects->triangles.size());
    CHECK_FALSE(job.take_completion());
}

TEST_CASE("Studio rejects a second start and can restart after a missing saved result", "[studio][job]")
{
    irop::test::TempDirectory temporary;
    const JobRequest preview_request = write_request(temporary.path(), JobKind::preview);
    JobRequest missing;
    missing.kind = JobKind::open;
    missing.summary_path = temporary.path() / "missing-summary.json";

    Job job;
    job.start(missing);
    irop::test::require_error_category([&] {
        job.start(preview_request);
    }, irop::ErrorCategory::invalid_configuration);
    // Even a worker that has already finished remains active until its single
    // completion is delivered; the failed second start cannot discard it.
    CHECK(job.active());
    const JobCompletion failed = wait_for_completion(job);
    CHECK_FALSE(failed.scene);
    CHECK_FALSE(failed.cancelled);
    CHECK_FALSE(failed.diagnostic.empty());
    CHECK_FALSE(job.active());
    CHECK_FALSE(job.take_completion());

    job.start(preview_request);
    const JobCompletion completed = wait_for_completion(job);
    INFO(completed.diagnostic);
    REQUIRE(completed.scene);
    CHECK(completed.scene->status == "input_preview");
    CHECK(completed.diagnostic.empty());
    CHECK_FALSE(job.active());
    CHECK_FALSE(job.progress());
}

}  // namespace
