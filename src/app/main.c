#include "../includes/libmkp/mkplayer_pl.h"

#include <libplacebo/renderer.h>
#include <libplacebo/vulkan.h>
#include <libplacebo/log.h>
#include <GLFW/glfw3.h>

#include <malloc.h>

typedef struct WindowPos {
    int x;
    int y;
    int w;
    int h;
} WindowPos;

typedef struct App {
    GLFWwindow *win;
    WindowPos windowed_pos;
    int close_window;

    pl_log log;
    pl_swapchain swapchain;
    struct pl_swapchain_frame sc_frame;
    pl_vulkan vk;
    pl_vk_inst vk_inst;
    VkSurfaceKHR surf;

    MkPlayer *player;
    char *src;
} App;

static void err_cb(int code, const char *desc)
{
    fprintf(stderr, "GLFW error %d: %s\n", code, desc);
}

static void resize_cb(GLFWwindow *win, int width, int height)
{
    App *app = glfwGetWindowUserPointer(win);
    if (!pl_swapchain_resize(app->swapchain, &width, &height)) {
        fprintf(stderr, "Failed resizing libplacebo swapchain.\n");
        app->close_window = true;
    }
}

static void close_cb(GLFWwindow *win)
{
    App *app = glfwGetWindowUserPointer(win);
    app->close_window = true;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
get_vk_proc_addr(VkInstance instance, const char* pName)
{
    return (PFN_vkVoidFunction) glfwGetInstanceProcAddress(instance, pName);
}

void destroy_app();

int initialize_app(App *app)
{
    app->log = pl_log_create(PL_API_VER, pl_log_params(
        .log_cb    = pl_log_color,
        .log_level = PL_LOG_WARN
    ));

    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize glfw.\n");
        goto end;
    }

    glfwSetErrorCallback(&err_cb);

    if (!glfwVulkanSupported()) {
        fprintf(stderr, "glfw doesn no support vulkan.\n");
        goto end;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    app->win = glfwCreateWindow(1920, 1080, "maikyno player", NULL, NULL);
    if (!app->win) {
        fprintf(stderr, "GLFW: Failed creating window\n");
        goto end;
    }

    glfwSetWindowUserPointer(app->win, app);
    glfwSetFramebufferSizeCallback(app->win, resize_cb);
    glfwSetWindowCloseCallback(app->win, close_cb);

    uint32_t num;
    app->vk_inst = pl_vk_inst_create(app->log, pl_vk_inst_params(
        .get_proc_addr = get_vk_proc_addr,
        .debug = true,
        .extensions = glfwGetRequiredInstanceExtensions(&num),
        .num_extensions = num
    ));
    if (!app->vk_inst) {
        fprintf(stderr, "libplacebo: Failed creating vulkan instance\n");
        goto end;
    }

    VkResult err;
    err = glfwCreateWindowSurface(app->vk_inst->instance,
        app->win, NULL, &app->surf);
    if (err != VK_SUCCESS) {
        fprintf(stderr, "GLFW: Failed creating vulkan surface\n");
        goto end;
    }

    app->vk = pl_vulkan_create(app->log, pl_vulkan_params(
        .instance = app->vk_inst->instance,
        .get_proc_addr = app->vk_inst->get_proc_addr,
        .surface = app->surf,
        .allow_software = true
    ));

    app->swapchain = pl_vulkan_create_swapchain(app->vk,
        pl_vulkan_swapchain_params(
            .surface = app->surf,
            .present_mode = VK_PRESENT_MODE_FIFO_KHR
    ));
    if (!app->swapchain) {
        fprintf(stderr, "libplacebo: Failed creating vulkan swapchain\n");
        goto end;
    }

    glfwGetWindowSize(app->win, &app->windowed_pos.w, &app->windowed_pos.h);
    glfwGetWindowPos(app->win, &app->windowed_pos.x, &app->windowed_pos.y);

    int w, h;
    glfwGetFramebufferSize(app->win, &w, &h);
    if (!pl_swapchain_resize(app->swapchain, &w, &h)) {
        fprintf(stderr, "libplacebo: Failed initializing swapchain\n");
        goto end;
    }

    return 0;

end:
    destroy_app();
    return -1;
}

void destroy_app()
{

}

int initial_render(App *app)
{
    if (!pl_swapchain_start_frame(app->swapchain, &app->sc_frame))
        return -1;

    struct pl_frame frame;
    pl_frame_from_swapchain(&frame, &app->sc_frame);

    if (mkp_render_from_pl_frame(app->player, &frame) < 0)
        return -1;

    if (!pl_swapchain_submit_frame(app->swapchain))
        return -1;

    pl_gpu_finish(app->vk->gpu);
    pl_swapchain_swap_buffers(app->swapchain);

    return 0;
}

int render_loop(App *app)
{
    struct pl_frame frame;

    while (!app->close_window) {
        if (!pl_swapchain_start_frame(app->swapchain, &app->sc_frame)) {
            glfwWaitEvents();
            continue;
        }

        pl_frame_from_swapchain(&frame, &app->sc_frame);

        if (mkp_render_from_pl_frame(app->player, &frame) < 0) {
            return -1;
        }

        if (!pl_swapchain_submit_frame(app->swapchain)) {
            fprintf(stderr, "libplacebo: failed presenting frame!\n");
            return -1;
        }

        pl_swapchain_swap_buffers(app->swapchain);
        glfwPollEvents();
    }

    return 0;
}

int main(int argc, char **argv)
{
    App *app = calloc(1, sizeof(App));
    app->src = "/media/hugexjackedman/Media Libraries/mk_movies/collections/007/17 - GoldenEye/GoldenEye.mkv";

    initialize_app(app);

    app->player = mkp_create_player_from_pl_vulkan(app->src,
        -1, 3, NULL, NULL, app->vk);
    if (!app->player)
        return -1;

    if (initial_render(app) < 0) {
        fprintf(stderr, "Failed initial render.\n");
        return -1;
    }

    render_loop(app);

    mkp_destroy_player(&app->player);
    destroy_app();

    return 0;
}
