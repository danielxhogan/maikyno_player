#include "../includes/libmkp/mkplayer.h"

#include <libplacebo/log.h>
#include <libplacebo/vulkan.h>
#include <GLFW/glfw3.h>

#include <malloc.h>

typedef struct App {
    GLFWwindow *win;
    int close_window;

    pl_log log;
    pl_swapchain swapchain;
    pl_vk_inst vk_inst;

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

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL get_vk_proc_addr(VkInstance instance, const char* pName)
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

    app->win = glfwCreateWindow(1920, 1080, "demo2 glfw vulkan", NULL, NULL);
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

    return 0;

end:
    destroy_app();
    return -1;
}

void destroy_app()
{

}

int main(int argc, char **argv)
{
    App *app = calloc(1, sizeof(App));
    app->src = "http://192.168.1.209:8080/media/tha_movies/mk_movies_2/collections/The Fast & The Furious Series/01 - The Fast And The Furious/The Fast And The Furious.mkv";

    initialize_app(app);

    app->player = mkp_create_player(app->src, 4);
    if (!app->player)
        return -1;

    while (1) {
        glfwPollEvents();
        if (app->close_window) {
            break;
        }
    }

    mkp_destroy_player(&app->player);
    destroy_app();

    return 0;
}
