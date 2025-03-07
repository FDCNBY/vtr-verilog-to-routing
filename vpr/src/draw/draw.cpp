/*********************************** Top-level Summary *************************************
 * This is VPR's main graphics application program. The program interacts with ezgl/graphics.hpp,
 * which provides an API for displaying graphics on both X11 and Win32. The most important
 * subroutine in this file is draw_main_canvas(), which is a callback function that will be called 
 * whenever the screen needs to be updated. Then, draw_main_canvas() will decide what
 * drawing subroutines to call depending on whether PLACEMENT or ROUTING is shown on screen.
 * The initial_setup_X() functions link the menu button signals to the corresponding drawing functions. 
 * As a note, looks into draw_global.c for understanding the data structures associated with drawing->
 *
 * 
 * Authors: Vaughn Betz, Long Yu (Mike) Wang, Dingyu (Tina) Yang
 * Last updated: June 2019
 */

#include <cstdio>
#include <cfloat>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <array>
#include <iostream>

#include "vtr_assert.h"
#include "vtr_ndoffsetmatrix.h"
#include "vtr_memory.h"
#include "vtr_log.h"
#include "vtr_color_map.h"
#include "vtr_path.h"

#include "vpr_utils.h"
#include "vpr_error.h"

#include "globals.h"
#include "draw_color.h"
#include "draw.h"
#include "read_xml_arch_file.h"
#include "draw_global.h"
#include "intra_logic_block.h"
#include "atom_netlist.h"
#include "tatum/report/TimingPathCollector.hpp"
#include "hsl.h"
#include "route_export.h"
#include "search_bar.h"
#include "save_graphics.h"
#include "timing_info.h"
#include "physical_types.h"
#include "route_common.h"
#include "breakpoint.h"
#include "manual_moves.h"

#include "move_utils.h"

#ifdef VTR_ENABLE_DEBUG_LOGGING
#    include "move_utils.h"
#endif

#ifdef WIN32 /* For runtime tracking in WIN32. The clock() function defined in time.h will *
              * track CPU runtime.														   */
#    include <time.h>
#else /* For X11. The clock() function in time.h will not output correct time difference   *
       * for X11, because the graphics is processed by the Xserver rather than local CPU,  *
       * which means tracking CPU time will not be the same as the actual wall clock time. *
       * Thus, so use gettimeofday() in sys/time.h to track actual calendar time.          */
#    include <sys/time.h>
#endif

#ifndef NO_GRAPHICS

//To process key presses we need the X11 keysym definitions,
//which are unavailable when building with MINGW
#    if defined(X11) && !defined(__MINGW32__)
#        include <X11/keysym.h>
#    endif

#    include "rr_graph.h"
#    include "route_util.h"
#    include "place_macro.h"
#    include "buttons.h"

/****************************** Define Macros *******************************/

#    define DEFAULT_RR_NODE_COLOR ezgl::BLACK
#    define OLD_BLK_LOC_COLOR blk_GOLD
#    define NEW_BLK_LOC_COLOR blk_GREEN
//#define TIME_DRAWSCREEN /* Enable if want to track runtime for drawscreen() */

/********************** Subroutines local to this module ********************/
static void drawplace(ezgl::renderer* g);
static void drawnets(ezgl::renderer* g);
static void drawroute(enum e_draw_net_type draw_net_type, ezgl::renderer* g);
static void draw_congestion(ezgl::renderer* g);
static void draw_routing_costs(ezgl::renderer* g);
static void draw_routing_bb(ezgl::renderer* g);
static void draw_routing_util(ezgl::renderer* g);
static void draw_crit_path(ezgl::renderer* g);
static void draw_placement_macros(ezgl::renderer* g);

void act_on_key_press(ezgl::application* /*app*/, GdkEventKey* /*event*/, char* key_name);
void act_on_mouse_press(ezgl::application* app, GdkEventButton* event, double x, double y);
void act_on_mouse_move(ezgl::application* app, GdkEventButton* event, double x, double y);

static void draw_routed_net(ClusterNetId net, ezgl::renderer* g);
void draw_partial_route(const std::vector<int>& rr_nodes_to_draw,
                        ezgl::renderer* g);
static void draw_rr(ezgl::renderer* g);
static void draw_rr_edges(int from_node, ezgl::renderer* g);
static void draw_rr_pin(int inode, const ezgl::color& color, ezgl::renderer* g);
static void draw_rr_chan(int inode, const ezgl::color color, ezgl::renderer* g);
static void draw_rr_src_sink(int inode, ezgl::color color, ezgl::renderer* g);
static void draw_pin_to_chan_edge(int pin_node, int chan_node, ezgl::renderer* g);
static void draw_get_rr_src_sink_coords(const t_rr_node& node, float* xcen, float* ycen);
static void draw_x(float x, float y, float size, ezgl::renderer* g);
static void draw_pin_to_pin(int opin, int ipin, ezgl::renderer* g);
static void draw_pin_to_sink(int ipin_node, int sink_node, ezgl::renderer* g);
static void draw_source_to_pin(int source_node, int opin_node, ezgl::renderer* g);
static void draw_rr_switch(float from_x, float from_y, float to_x, float to_y, bool buffered, bool switch_configurable, ezgl::renderer* g);
static void draw_chany_to_chany_edge(RRNodeId from_node, RRNodeId to_node, int to_track, short switch_type, ezgl::renderer* g);
static void draw_chanx_to_chanx_edge(RRNodeId from_node, RRNodeId to_node, int to_track, short switch_type, ezgl::renderer* g);
static void draw_chanx_to_chany_edge(int chanx_node, int chanx_track, int chany_node, int chany_track, enum e_edge_dir edge_dir, short switch_type, ezgl::renderer* g);
static int get_track_num(int inode, const vtr::OffsetMatrix<int>& chanx_track, const vtr::OffsetMatrix<int>& chany_track);
static bool draw_if_net_highlighted(ClusterNetId inet);
static int draw_check_rr_node_hit(float click_x, float click_y);

static void draw_expand_non_configurable_rr_nodes_recurr(int from_node,
                                                         std::set<int>& expanded_nodes);
static bool highlight_rr_nodes(float x, float y);
static void highlight_blocks(double x, double y);
static void draw_reset_blk_colors();
static void draw_reset_blk_color(ClusterBlockId blk_id);

static inline void draw_mux_with_size(ezgl::point2d origin, e_side orientation, float height, int size, ezgl::renderer* g);
static inline ezgl::rectangle draw_mux(ezgl::point2d origin, e_side orientation, float height, ezgl::renderer* g);
static inline ezgl::rectangle draw_mux(ezgl::point2d origin, e_side orientation, float height, float width, float height_scale, ezgl::renderer* g);

static void draw_flyline_timing_edge(ezgl::point2d start, ezgl::point2d end, float incr_delay, ezgl::renderer* g);
static void draw_routed_timing_edge(tatum::NodeId start_tnode,
                                    tatum::NodeId end_tnode,
                                    float incr_delay,
                                    ezgl::color color,
                                    ezgl::renderer* g);
static void draw_routed_timing_edge_connection(tatum::NodeId src_tnode,
                                               tatum::NodeId sink_tnode,
                                               ezgl::color color,
                                               ezgl::renderer* g);
static std::vector<int> trace_routed_connection_rr_nodes(
    const ClusterNetId net_id,
    const int driver_pin,
    const int sink_pin);
static bool trace_routed_connection_rr_nodes_recurr(const t_rt_node* rt_node,
                                                    int sink_rr_node,
                                                    std::vector<int>& rr_nodes_on_path);
static t_edge_size find_edge(int prev_inode, int inode);

static void draw_color_map_legend(const vtr::ColorMap& cmap, ezgl::renderer* g);

ezgl::color lighten_color(ezgl::color color, float amount);

static void draw_block_pin_util();

static float get_router_expansion_cost(const t_rr_node_route_inf node_inf,
                                       e_draw_router_expansion_cost draw_router_expansion_cost);
static void draw_router_expansion_costs(ezgl::renderer* g);

static void draw_rr_costs(ezgl::renderer* g, const std::vector<float>& rr_costs, bool lowest_cost_first = true);
static void draw_main_canvas(ezgl::renderer* g);
static void initial_setup_NO_PICTURE_to_PLACEMENT(ezgl::application* app,
                                                  bool is_new_window);
static void initial_setup_NO_PICTURE_to_PLACEMENT_with_crit_path(
    ezgl::application* app,
    bool is_new_window);
static void initial_setup_PLACEMENT_to_ROUTING(ezgl::application* app,
                                               bool is_new_window);
static void initial_setup_ROUTING_to_PLACEMENT(ezgl::application* app,
                                               bool is_new_window);
static void initial_setup_NO_PICTURE_to_ROUTING(ezgl::application* app,
                                                bool is_new_window);
static void initial_setup_NO_PICTURE_to_ROUTING_with_crit_path(
    ezgl::application* app,
    bool is_new_window);
static void toggle_window_mode(GtkWidget* /*widget*/,
                               ezgl::application* /*app*/);
static void setup_default_ezgl_callbacks(ezgl::application* app);
static void set_force_pause(GtkWidget* /*widget*/, gint /*response_id*/, gpointer /*data*/);
static void set_block_outline(GtkWidget* widget, gint /*response_id*/, gpointer /*data*/);
static void set_block_text(GtkWidget* widget, gint /*response_id*/, gpointer /*data*/);
static void clip_routing_util(GtkWidget* widget, gint /*response_id*/, gpointer /*data*/);
static void run_graphics_commands(std::string commands);

/************************** File Scope Variables ****************************/

//The arrow head position for turning/straight-thru connections in a switch box
constexpr float SB_EDGE_TURN_ARROW_POSITION = 0.2;
constexpr float SB_EDGE_STRAIGHT_ARROW_POSITION = 0.95;
constexpr float EMPTY_BLOCK_LIGHTEN_FACTOR = 0.20;

//Kelly's maximum contrast colors are selected to be easily distinguishable as described in:
//  Kenneth Kelly, "Twenty-Two Colors of Maximum Contrast", Color Eng. 3(6), 1943
//We use these to highlight a relatively small number of things (e.g. stages in a critical path,
//a subset of selected net) where it is important for them to be visually distinct.

const std::vector<ezgl::color> kelly_max_contrast_colors = {
    //ezgl::color(242, 243, 244), //white: skip white since it doesn't contrast well with VPR's light background
    ezgl::color(34, 34, 34),    //black
    ezgl::color(243, 195, 0),   //yellow
    ezgl::color(135, 86, 146),  //purple
    ezgl::color(243, 132, 0),   //orange
    ezgl::color(161, 202, 241), //light blue
    ezgl::color(190, 0, 50),    //red
    ezgl::color(194, 178, 128), //buf
    ezgl::color(132, 132, 130), //gray
    ezgl::color(0, 136, 86),    //green
    ezgl::color(230, 143, 172), //purplish pink
    ezgl::color(0, 103, 165),   //blue
    ezgl::color(249, 147, 121), //yellowish pink
    ezgl::color(96, 78, 151),   //violet
    ezgl::color(246, 166, 0),   //orange yellow
    ezgl::color(179, 68, 108),  //purplish red
    ezgl::color(220, 211, 0),   //greenish yellow
    ezgl::color(136, 45, 23),   //redish brown
    ezgl::color(141, 182, 0),   //yellow green
    ezgl::color(101, 69, 34),   //yellowish brown
    ezgl::color(226, 88, 34),   //reddish orange
    ezgl::color(43, 61, 38)     //olive green
};

ezgl::application::settings settings("/ezgl/main.ui", "MainWindow", "MainCanvas", "org.verilogtorouting.vpr.PID" + std::to_string(vtr::get_pid()), setup_default_ezgl_callbacks);
ezgl::application application(settings);

bool window_mode = false;
bool window_point_1_collected = false;
ezgl::point2d point_1(0, 0);
ezgl::rectangle initial_world;
std::string rr_highlight_message;

#endif // NO_GRAPHICS

/********************** Subroutine definitions ******************************/

void init_graphics_state(bool show_graphics_val, int gr_automode_val, enum e_route_type route_type, bool save_graphics, std::string graphics_commands) {
#ifndef NO_GRAPHICS
    /* Call accessor functions to retrieve global variables. */
    t_draw_state* draw_state = get_draw_state_vars();

    /* Sets the static show_graphics and gr_automode variables to the    *
     * desired values.  They control if graphics are enabled and, if so, *
     * how often the user is prompted for input.                         */

    draw_state->show_graphics = show_graphics_val;
    draw_state->gr_automode = gr_automode_val;
    draw_state->draw_route_type = route_type;
    draw_state->save_graphics = save_graphics;
    draw_state->graphics_commands = graphics_commands;

#else
    //Suppress unused parameter warnings
    (void)show_graphics_val;
    (void)gr_automode_val;
    (void)route_type;
    (void)save_graphics;
    (void)graphics_commands;
#endif // NO_GRAPHICS
}

#ifndef NO_GRAPHICS
static void draw_main_canvas(ezgl::renderer* g) {
    t_draw_state* draw_state = get_draw_state_vars();

    g->set_font_size(14);

    draw_block_pin_util();
    drawplace(g);
    draw_internal_draw_subblk(g);

    if (draw_state->pic_on_screen == PLACEMENT) {
        switch (draw_state->show_nets) {
            case DRAW_NETS:
                drawnets(g);
                break;
            case DRAW_LOGICAL_CONNECTIONS:
                break;
            default:
                break;
        }
    } else { /* ROUTING on screen */

        switch (draw_state->show_nets) {
            case DRAW_NETS:
                drawroute(ALL_NETS, g);
                break;
            case DRAW_LOGICAL_CONNECTIONS:
                // fall through
            default:
                draw_rr(g);
                break;
        }

        draw_congestion(g);

        draw_routing_costs(g);

        draw_router_expansion_costs(g);

        draw_routing_util(g);

        draw_routing_bb(g);
    }

    draw_placement_macros(g);

    draw_crit_path(g);

    draw_logical_connections(g);

    if (draw_state->color_map) {
        draw_color_map_legend(*draw_state->color_map, g);
        draw_state->color_map.reset(); //Free color map in preparation for next redraw
    }

    if (draw_state->auto_proceed) {
        //Automatically exit the event loop, so user's don't need to manually click proceed

        //Avoid trying to repeatedly exit (which would cause errors in GTK)
        draw_state->auto_proceed = false;

        application.quit(); //Ensure we leave the event loop
    }
}

/* function below intializes the interface window with a set of buttons and links 
 * signals to corresponding functions for situation where the window is opened from 
 * NO_PICTURE_to_PLACEMENT */
static void initial_setup_NO_PICTURE_to_PLACEMENT(ezgl::application* app,
                                                  bool is_new_window) {
    if (!is_new_window)
        return;

    //button to enter window_mode, created in main.ui
    GtkButton* window = (GtkButton*)app->get_object("Window");
    gtk_button_set_label(window, "Window");
    g_signal_connect(window, "clicked", G_CALLBACK(toggle_window_mode), app);

    //button to search, created in main.ui
    GtkButton* search = (GtkButton*)app->get_object("Search");
    gtk_button_set_label(search, "Search");
    g_signal_connect(search, "clicked", G_CALLBACK(search_and_highlight), app);

    //button for save graphcis, created in main.ui
    GtkButton* save = (GtkButton*)app->get_object("SaveGraphics");
    g_signal_connect(save, "clicked", G_CALLBACK(save_graphics_dialog_box),
                     app);

    //combo box for search type, created in main.ui
    GObject* search_type = (GObject*)app->get_object("SearchType");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(search_type), "Block ID"); // index 0
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(search_type),
                                   "Block Name");                                // index 1
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(search_type), "Net ID");   // index 2
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(search_type), "Net Name"); // index 3
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(search_type),
                                   "RR Node ID");           // index 4
    gtk_combo_box_set_active((GtkComboBox*)search_type, 0); // default set to Block ID which has an index 0

    button_for_toggle_nets();
    button_for_net_max_fanout();
    button_for_net_alpha();
    button_for_toggle_blk_internal();
    button_for_toggle_block_pin_util();
    button_for_toggle_placement_macros();
}

/* function below intializes the interface window with a set of buttons and links 
 * signals to corresponding functions for situation where the window is opened from 
 * NO_PICTURE_to_PLACEMENT_with_crit_path */
static void initial_setup_NO_PICTURE_to_PLACEMENT_with_crit_path(
    ezgl::application* app,
    bool is_new_window) {
    initial_setup_NO_PICTURE_to_PLACEMENT(app, is_new_window);
    button_for_toggle_crit_path();
}

/* function below intializes the interface window with a set of buttons and links 
 * signals to corresponding functions for situation where the window is opened from 
 * PLACEMENT_to_ROUTING */
static void initial_setup_PLACEMENT_to_ROUTING(ezgl::application* app,
                                               bool is_new_window) {
    initial_setup_NO_PICTURE_to_PLACEMENT_with_crit_path(app, is_new_window);
    button_for_toggle_rr();
    button_for_toggle_congestion();
    button_for_toggle_congestion_cost();
    button_for_toggle_routing_bounding_box();
    button_for_toggle_routing_util();
    button_for_toggle_router_expansion_costs();
}

/* function below intializes the interface window with a set of buttons and links 
 * signals to corresponding functions for situation where the window is opened from 
 * ROUTING_to_PLACEMENT */
static void initial_setup_ROUTING_to_PLACEMENT(ezgl::application* app,
                                               bool is_new_window) {
    initial_setup_PLACEMENT_to_ROUTING(app, is_new_window);
    std::string toggle_rr = "toggle_rr";
    std::string toggle_congestion = "toggle_congestion";
    std::string toggle_routing_congestion_cost = "toggle_routing_congestion_cost";
    std::string toggle_routing_bounding_box = "toggle_routing_bounding_box";
    std::string toggle_routing_util = "toggle_rr";
    std::string toggle_router_expansion_costs = "toggle_router_expansion_costs";

    delete_button(toggle_rr.c_str());
    delete_button(toggle_congestion.c_str());
    delete_button(toggle_routing_congestion_cost.c_str());
    delete_button(toggle_routing_bounding_box.c_str());
    delete_button(toggle_routing_util.c_str());
    delete_button(toggle_router_expansion_costs.c_str());
}

/* function below intializes the interface window with a set of buttons and links 
 * signals to corresponding functions for situation where the window is opened from 
 * NO_PICTURE_to_ROUTING */
static void initial_setup_NO_PICTURE_to_ROUTING(ezgl::application* app,
                                                bool is_new_window) {
    if (!is_new_window)
        return;

    GtkButton* window = (GtkButton*)app->get_object("Window");
    gtk_button_set_label(window, "Window");
    g_signal_connect(window, "clicked", G_CALLBACK(toggle_window_mode), app);

    GtkButton* search = (GtkButton*)app->get_object("Search");
    gtk_button_set_label(search, "Search");
    g_signal_connect(search, "clicked", G_CALLBACK(search_and_highlight), app);

    GtkButton* save = (GtkButton*)app->get_object("SaveGraphics");
    g_signal_connect(save, "clicked", G_CALLBACK(save_graphics_dialog_box),
                     app);

    GObject* search_type = (GObject*)app->get_object("SearchType");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(search_type), "Block ID");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(search_type),
                                   "Block Name");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(search_type), "Net ID");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(search_type), "Net Name");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(search_type),
                                   "RR Node ID");

    button_for_toggle_nets();
    button_for_net_max_fanout();
    button_for_net_alpha();
    button_for_toggle_blk_internal();
    button_for_toggle_block_pin_util();
    button_for_toggle_placement_macros();
    button_for_toggle_rr();
    button_for_toggle_congestion();
    button_for_toggle_congestion_cost();
    button_for_toggle_routing_bounding_box();
    button_for_toggle_routing_util();
    button_for_toggle_router_expansion_costs();
}

/* function below intializes the interface window with a set of buttons and links 
 * signals to corresponding functions for situation where the window is opened from 
 * NO_PICTURE_to_ROUTING_with_crit_path */
static void initial_setup_NO_PICTURE_to_ROUTING_with_crit_path(
    ezgl::application* app,
    bool is_new_window) {
    initial_setup_NO_PICTURE_to_ROUTING(app, is_new_window);
    button_for_toggle_crit_path();
}
#endif //NO_GRAPHICS

void update_screen(ScreenUpdatePriority priority, const char* msg, enum pic_type pic_on_screen_val, std::shared_ptr<SetupTimingInfo> setup_timing_info) {
#ifndef NO_GRAPHICS

    /* Updates the screen if the user has requested graphics.  The priority  *
     * value controls whether or not the Proceed button must be clicked to   *
     * continue.  Saves the pic_on_screen_val to allow pan and zoom redraws. */

    t_draw_state* draw_state = get_draw_state_vars();

    if (!draw_state->show_graphics)
        ezgl::set_disable_event_loop(true);
    else
        ezgl::set_disable_event_loop(false);

    ezgl::setup_callback_fn init_setup = nullptr;

    /* If it's the type of picture displayed has changed, set up the proper  *
     * buttons.                                                              */
    if (draw_state->pic_on_screen != pic_on_screen_val) { //State changed

        if (draw_state->pic_on_screen == NO_PICTURE) {
            //Only add the canvas the first time we open graphics
            application.add_canvas("MainCanvas", draw_main_canvas,
                                   initial_world);
        }

        draw_state->setup_timing_info = setup_timing_info;

        if (pic_on_screen_val == PLACEMENT
            && draw_state->pic_on_screen == NO_PICTURE) {
            if (setup_timing_info) {
                init_setup = initial_setup_NO_PICTURE_to_PLACEMENT_with_crit_path;
            } else {
                init_setup = initial_setup_NO_PICTURE_to_PLACEMENT;
            }
            draw_state->save_graphics_file_base = "vpr_placement";

        } else if (pic_on_screen_val == ROUTING
                   && draw_state->pic_on_screen == PLACEMENT) {
            //Routing, opening after placement
            init_setup = initial_setup_PLACEMENT_to_ROUTING;
            draw_state->save_graphics_file_base = "vpr_routing";

        } else if (pic_on_screen_val == PLACEMENT
                   && draw_state->pic_on_screen == ROUTING) {
            init_setup = initial_setup_ROUTING_to_PLACEMENT;
            draw_state->save_graphics_file_base = "vpr_placement";

        } else if (pic_on_screen_val == ROUTING
                   && draw_state->pic_on_screen == NO_PICTURE) {
            //Routing opening first
            if (setup_timing_info) {
                init_setup = initial_setup_NO_PICTURE_to_ROUTING_with_crit_path;
            } else {
                init_setup = initial_setup_NO_PICTURE_to_ROUTING;
            }
            draw_state->save_graphics_file_base = "vpr_routing";
        }

        draw_state->pic_on_screen = pic_on_screen_val;

    } else {
        //No change (e.g. paused)
        init_setup = nullptr;
    }

    bool state_change = (init_setup != nullptr);
    bool should_pause = int(priority) >= draw_state->gr_automode;

    //If there was a state change, we must call ezgl::application::run() to update the buttons.
    //However, by default this causes graphics to pause for user interaction.
    //
    //If the priority is such that we shouldn't pause we need to continue automatically, so
    //the user won't need to click manually.
    draw_state->auto_proceed = (state_change && !should_pause);

    if (state_change                   //Must update buttons
        || should_pause                //The priority means graphics should pause for user interaction
        || draw_state->forced_pause) { //The user asked to pause

        if (draw_state->forced_pause) {
            VTR_LOG("Pausing in interactive graphics (user pressed 'Pause')\n");
            draw_state->forced_pause = false; //Reset pause flag
        }

        application.run(init_setup, act_on_mouse_press, act_on_mouse_move,
                        act_on_key_press);

        if (!draw_state->graphics_commands.empty()) {
            run_graphics_commands(draw_state->graphics_commands);
        }
    }

    if (draw_state->show_graphics) {
        application.update_message(msg);
        application.refresh_drawing();
        application.flush_drawing();
    }

    if (draw_state->save_graphics) {
        std::string extension = "pdf";
        save_graphics(extension, draw_state->save_graphics_file_base);
    }

#else
    (void)setup_timing_info;
    (void)priority;
    (void)msg;
    (void)pic_on_screen_val;
#endif //NO_GRAPHICS
}

#ifndef NO_GRAPHICS
static void toggle_window_mode(GtkWidget* /*widget*/,
                               ezgl::application* /*app*/) {
    window_mode = true;
}

void toggle_nets(GtkWidget* /*widget*/, gint /*response_id*/, gpointer /*data*/) {
    /* this is the callback function for runtime created toggle_nets button
     * which is written in button.cpp                                         */
    t_draw_state* draw_state = get_draw_state_vars();

    // get the pointer to the toggle_nets button
    std::string button_name = "toggle_nets";
    auto toggle_nets = find_button(button_name.c_str());

    // use the pointer to get the active text
    enum e_draw_nets new_state;
    gchar* combo_box_content = gtk_combo_box_text_get_active_text(
        GTK_COMBO_BOX_TEXT(toggle_nets));

    // assign corresponding enum value to draw_state->show_nets
    if (strcmp(combo_box_content, "None") == 0)
        new_state = DRAW_NO_NETS;
    else if (strcmp(combo_box_content, "Nets") == 0) {
        new_state = DRAW_NETS;
    } else { // "Logical Connections"
        new_state = DRAW_LOGICAL_CONNECTIONS;
    }
    draw_state->reset_nets_congestion_and_rr();
    draw_state->show_nets = new_state;

    //free dynamically allocated pointers
    g_free(combo_box_content);

    //redraw
    application.update_message(draw_state->default_message);
    application.refresh_drawing();
}

void toggle_rr(GtkWidget* /*widget*/, gint /*response_id*/, gpointer /*data*/) {
    /* this is the callback function for runtime created toggle_rr button
     * which is written in button.cpp                                         */
    t_draw_state* draw_state = get_draw_state_vars();
    std::string button_name = "toggle_rr";
    auto toggle_rr = find_button(button_name.c_str());

    enum e_draw_rr_toggle new_state;
    gchar* combo_box_content = gtk_combo_box_text_get_active_text(
        GTK_COMBO_BOX_TEXT(toggle_rr));
    if (strcmp(combo_box_content, "None") == 0)
        new_state = DRAW_NO_RR;
    else if (strcmp(combo_box_content, "Nodes") == 0)
        new_state = DRAW_NODES_RR;
    else if (strcmp(combo_box_content, "Nodes SBox") == 0)
        new_state = DRAW_NODES_SBOX_RR;
    else if (strcmp(combo_box_content, "Nodes SBox CBox") == 0)
        new_state = DRAW_NODES_SBOX_CBOX_RR;
    else if (strcmp(combo_box_content, "Nodes SBox CBox Internal") == 0)
        new_state = DRAW_NODES_SBOX_CBOX_INTERNAL_RR;
    else
        // all rr
        new_state = DRAW_ALL_RR;

    //free dynamically allocated pointers
    g_free(combo_box_content);

    draw_state->reset_nets_congestion_and_rr();
    draw_state->draw_rr_toggle = new_state;

    application.update_message(draw_state->default_message);
    application.refresh_drawing();
}

void toggle_congestion(GtkWidget* /*widget*/, gint /*response_id*/, gpointer /*data*/) {
    /* this is the callback function for runtime created toggle_congestion button
     * which is written in button.cpp                                         */
    t_draw_state* draw_state = get_draw_state_vars();
    std::string button_name = "toggle_congestion";
    auto toggle_congestion = find_button(button_name.c_str());

    enum e_draw_congestion new_state;
    gchar* combo_box_content = gtk_combo_box_text_get_active_text(
        GTK_COMBO_BOX_TEXT(toggle_congestion));
    if (strcmp(combo_box_content, "None") == 0)
        new_state = DRAW_NO_CONGEST;
    else if (strcmp(combo_box_content, "Congested") == 0)
        new_state = DRAW_CONGESTED;
    else
        // congested with nets
        new_state = DRAW_CONGESTED_WITH_NETS;

    draw_state->reset_nets_congestion_and_rr();
    draw_state->show_congestion = new_state;
    if (draw_state->show_congestion == DRAW_NO_CONGEST) {
        application.update_message(draw_state->default_message);
    }
    g_free(combo_box_content);
    application.refresh_drawing();
}

void toggle_routing_congestion_cost(GtkWidget* /*widget*/, gint /*response_id*/, gpointer /*data*/) {
    /* this is the callback function for runtime created toggle_routing_congestion_cost button
     * which is written in button.cpp                                         */
    t_draw_state* draw_state = get_draw_state_vars();
    std::string button_name = "toggle_routing_congestion_cost";
    auto toggle_routing_congestion_cost = find_button(button_name.c_str());
    enum e_draw_routing_costs new_state;
    gchar* combo_box_content = gtk_combo_box_text_get_active_text(
        GTK_COMBO_BOX_TEXT(toggle_routing_congestion_cost));
    if (strcmp(combo_box_content, "None") == 0)
        new_state = DRAW_NO_ROUTING_COSTS;
    else if (strcmp(combo_box_content, "Total Routing Costs") == 0)
        new_state = DRAW_TOTAL_ROUTING_COSTS;
    else if (strcmp(combo_box_content, "Log Total Routing Costs") == 0)
        new_state = DRAW_LOG_TOTAL_ROUTING_COSTS;
    else if (strcmp(combo_box_content, "Acc Routing Costs") == 0)
        new_state = DRAW_ACC_ROUTING_COSTS;
    else if (strcmp(combo_box_content, "Log Acc Routing Costs") == 0)
        new_state = DRAW_LOG_ACC_ROUTING_COSTS;
    else if (strcmp(combo_box_content, "Pres Routing Costs") == 0)
        new_state = DRAW_PRES_ROUTING_COSTS;
    else if (strcmp(combo_box_content, "Log Pres Routing Costs") == 0)
        new_state = DRAW_LOG_PRES_ROUTING_COSTS;
    else
        new_state = DRAW_BASE_ROUTING_COSTS;

    draw_state->reset_nets_congestion_and_rr();
    draw_state->show_routing_costs = new_state;
    g_free(combo_box_content);
    if (draw_state->show_routing_costs == DRAW_NO_ROUTING_COSTS) {
        application.update_message(draw_state->default_message);
    }
    application.refresh_drawing();
}

void toggle_routing_bounding_box(GtkWidget* /*widget*/, gint /*response_id*/, gpointer /*data*/) {
    /* this is the callback function for runtime created toggle_routing_bounding_box button
     * which is written in button.cpp                                         */
    t_draw_state* draw_state = get_draw_state_vars();
    auto& route_ctx = g_vpr_ctx.routing();
    // get the pointer to the toggle_routing_bounding_box button
    std::string button_name = "toggle_routing_bounding_box";
    auto toggle_routing_bounding_box = find_button(button_name.c_str());

    if (route_ctx.route_bb.size() == 0)
        return; //Nothing to draw

    // use the pointer to get the active value
    int new_value = gtk_spin_button_get_value_as_int(
        (GtkSpinButton*)toggle_routing_bounding_box);

    // assign value to draw_state->show_routing_bb, bound check + set OPEN when it's -1 (draw nothing)
    if (new_value < -1)
        draw_state->show_routing_bb = -1;
    else if (new_value == -1)
        draw_state->show_routing_bb = OPEN;
    else if (new_value >= (int)(route_ctx.route_bb.size()))
        draw_state->show_routing_bb = route_ctx.route_bb.size() - 1;
    else
        draw_state->show_routing_bb = new_value;

    //redraw
    if ((int)(draw_state->show_routing_bb)
        == (int)((int)(route_ctx.route_bb.size()) - 1)) {
        application.update_message(draw_state->default_message);
    }
    application.refresh_drawing();
}

void toggle_routing_util(GtkWidget* /*widget*/, gint /*response_id*/, gpointer /*data*/) {
    /* this is the callback function for runtime created toggle_routing_util button
     * which is written in button.cpp                                         */
    t_draw_state* draw_state = get_draw_state_vars();
    std::string button_name = "toggle_routing_util";
    auto toggle_routing_util = find_button(button_name.c_str());

    enum e_draw_routing_util new_state;
    gchar* combo_box_content = gtk_combo_box_text_get_active_text(
        GTK_COMBO_BOX_TEXT(toggle_routing_util));
    if (strcmp(combo_box_content, "None") == 0)
        new_state = DRAW_NO_ROUTING_UTIL;
    else if (strcmp(combo_box_content, "Routing Util") == 0)
        new_state = DRAW_ROUTING_UTIL;
    else if (strcmp(combo_box_content, "Routing Util with Value") == 0)
        new_state = DRAW_ROUTING_UTIL_WITH_VALUE;
    else if (strcmp(combo_box_content, "Routing Util with Formula") == 0)
        new_state = DRAW_ROUTING_UTIL_WITH_FORMULA;
    else
        new_state = DRAW_ROUTING_UTIL_OVER_BLOCKS;

    g_free(combo_box_content);
    draw_state->show_routing_util = new_state;

    if (draw_state->show_routing_util == DRAW_NO_ROUTING_UTIL) {
        application.update_message(draw_state->default_message);
    }
    application.refresh_drawing();
}

void toggle_blk_internal(GtkWidget* /*widget*/, gint /*response_id*/, gpointer /*data*/) {
    /* this is the callback function for runtime created toggle_blk_internal button
     * which is written in button.cpp                                         */
    t_draw_state* draw_state = get_draw_state_vars();
    std::string button_name = "toggle_blk_internal";
    auto toggle_blk_internal = find_button(button_name.c_str());

    int new_value = gtk_spin_button_get_value_as_int(
        (GtkSpinButton*)toggle_blk_internal);
    if (new_value < 0)
        draw_state->show_blk_internal = 0;
    else if (new_value >= draw_state->max_sub_blk_lvl)
        draw_state->show_blk_internal = draw_state->max_sub_blk_lvl - 1;
    else
        draw_state->show_blk_internal = new_value;
    application.refresh_drawing();
}

void toggle_block_pin_util(GtkWidget* /*widget*/, gint /*response_id*/, gpointer /*data*/) {
    /* this is the callback function for runtime created toggle_block_pin_util button
     * which is written in button.cpp                                         */
    t_draw_state* draw_state = get_draw_state_vars();
    std::string button_name = "toggle_block_pin_util";
    auto toggle_block_pin_util = find_button(button_name.c_str());
    gchar* combo_box_content = gtk_combo_box_text_get_active_text(
        GTK_COMBO_BOX_TEXT(toggle_block_pin_util));
    if (strcmp(combo_box_content, "None") == 0) {
        draw_state->show_blk_pin_util = DRAW_NO_BLOCK_PIN_UTIL;
        draw_reset_blk_colors();
        application.update_message(draw_state->default_message);
    } else if (strcmp(combo_box_content, "All") == 0)
        draw_state->show_blk_pin_util = DRAW_BLOCK_PIN_UTIL_TOTAL;
    else if (strcmp(combo_box_content, "Inputs") == 0)
        draw_state->show_blk_pin_util = DRAW_BLOCK_PIN_UTIL_INPUTS;
    else
        draw_state->show_blk_pin_util = DRAW_BLOCK_PIN_UTIL_OUTPUTS;

    g_free(combo_box_content);
    application.refresh_drawing();
}

void toggle_placement_macros(GtkWidget* /*widget*/, gint /*response_id*/, gpointer /*data*/) {
    /* this is the callback function for runtime created toggle_placement_macros button
     * which is written in button.cpp                                         */
    t_draw_state* draw_state = get_draw_state_vars();
    std::string button_name = "toggle_placement_macros";
    auto toggle_placement_macros = find_button(button_name.c_str());

    gchar* combo_box_content = gtk_combo_box_text_get_active_text(
        GTK_COMBO_BOX_TEXT(toggle_placement_macros));
    if (strcmp(combo_box_content, "None") == 0)
        draw_state->show_placement_macros = DRAW_NO_PLACEMENT_MACROS;
    else
        draw_state->show_placement_macros = DRAW_PLACEMENT_MACROS;

    g_free(combo_box_content);
    application.refresh_drawing();
}

void toggle_crit_path(GtkWidget* /*widget*/, gint /*response_id*/, gpointer /*data*/) {
    /* this is the callback function for runtime created toggle_crit_path button
     * which is written in button.cpp                                         */
    t_draw_state* draw_state = get_draw_state_vars();
    std::string button_name = "toggle_crit_path";
    auto toggle_crit_path = find_button(button_name.c_str());

    gchar* combo_box_content = gtk_combo_box_text_get_active_text(
        GTK_COMBO_BOX_TEXT(toggle_crit_path));
    if (strcmp(combo_box_content, "None") == 0) {
        draw_state->show_crit_path = DRAW_NO_CRIT_PATH;
    } else if (strcmp(combo_box_content, "Crit Path Flylines") == 0)
        draw_state->show_crit_path = DRAW_CRIT_PATH_FLYLINES;
    else if (strcmp(combo_box_content, "Crit Path Flylines Delays") == 0)
        draw_state->show_crit_path = DRAW_CRIT_PATH_FLYLINES_DELAYS;
    else if (strcmp(combo_box_content, "Crit Path Routing") == 0)
        draw_state->show_crit_path = DRAW_CRIT_PATH_ROUTING;
    else
        // Crit Path Routing Delays
        draw_state->show_crit_path = DRAW_CRIT_PATH_ROUTING_DELAYS;

    g_free(combo_box_content);
    application.refresh_drawing();
}

void toggle_router_expansion_costs(GtkWidget* /*widget*/, gint /*response_id*/, gpointer /*data*/) {
    /* this is the callback function for runtime created toggle_router_expansion_costs button
     * which is written in button.cpp                                         */
    t_draw_state* draw_state = get_draw_state_vars();
    std::string button_name = "toggle_router_expansion_costs";
    auto toggle_router_expansion_costs = find_button(button_name.c_str());

    e_draw_router_expansion_cost new_state;
    gchar* combo_box_content = gtk_combo_box_text_get_active_text(
        GTK_COMBO_BOX_TEXT(toggle_router_expansion_costs));
    if (strcmp(combo_box_content, "None") == 0) {
        new_state = DRAW_NO_ROUTER_EXPANSION_COST;
    } else if (strcmp(combo_box_content, "Total") == 0) {
        new_state = DRAW_ROUTER_EXPANSION_COST_TOTAL;
    } else if (strcmp(combo_box_content, "Known") == 0) {
        new_state = DRAW_ROUTER_EXPANSION_COST_KNOWN;
    } else if (strcmp(combo_box_content, "Expected") == 0) {
        new_state = DRAW_ROUTER_EXPANSION_COST_EXPECTED;
    } else if (strcmp(combo_box_content, "Total (with edges)") == 0) {
        new_state = DRAW_ROUTER_EXPANSION_COST_TOTAL_WITH_EDGES;
    } else if (strcmp(combo_box_content, "Known (with edges)") == 0) {
        new_state = DRAW_ROUTER_EXPANSION_COST_KNOWN_WITH_EDGES;
    } else if (strcmp(combo_box_content, "Expected (with edges)") == 0) {
        new_state = DRAW_ROUTER_EXPANSION_COST_EXPECTED_WITH_EDGES;
    } else {
        VPR_THROW(VPR_ERROR_DRAW, "Unrecognzied draw RR cost option");
    }

    g_free(combo_box_content);
    draw_state->show_router_expansion_cost = new_state;

    if (draw_state->show_router_expansion_cost
        == DRAW_NO_ROUTER_EXPANSION_COST) {
        application.update_message(draw_state->default_message);
    }
    application.refresh_drawing();
}

#endif // NO_GRAPHICS

void alloc_draw_structs(const t_arch* arch) {
#ifndef NO_GRAPHICS
    /* Call accessor functions to retrieve global variables. */
    t_draw_coords* draw_coords = get_draw_coords_vars();
    t_draw_state* draw_state = get_draw_state_vars();
    auto& device_ctx = g_vpr_ctx.device();
    auto& cluster_ctx = g_vpr_ctx.clustering();

    /* Allocate the structures needed to draw the placement and routing->  Set *
     * up the default colors for blocks and nets.                             */
    draw_coords->tile_x = (float*)vtr::malloc(
        device_ctx.grid.width() * sizeof(float));
    draw_coords->tile_y = (float*)vtr::malloc(
        device_ctx.grid.height() * sizeof(float));

    /* For sub-block drawings inside clbs */
    draw_internal_alloc_blk();

    draw_state->net_color.resize(cluster_ctx.clb_nlist.nets().size());
    draw_state->block_color_.resize(cluster_ctx.clb_nlist.blocks().size());
    draw_state->use_default_block_color_.resize(
        cluster_ctx.clb_nlist.blocks().size());

    /* Space is allocated for draw_rr_node but not initialized because we do *
     * not yet know information about the routing resources.				  */
    draw_state->draw_rr_node = (t_draw_rr_node*)vtr::malloc(
        device_ctx.rr_nodes.size() * sizeof(t_draw_rr_node));

    draw_state->arch_info = arch;

    deselect_all(); /* Set initial colors */
#else
    (void)arch;
#endif // NO_GRAPHICS
}

void free_draw_structs() {
#ifndef NO_GRAPHICS
    /* Free everything allocated by alloc_draw_structs. Called after close_graphics() *
     * in vpr_api.c.
     *
     * For safety, set all the array pointers to NULL in case any data
     * structure gets freed twice.													 */
    t_draw_state* draw_state = get_draw_state_vars();
    t_draw_coords* draw_coords = get_draw_coords_vars();

    if (draw_coords != nullptr) {
        free(draw_coords->tile_x);
        draw_coords->tile_x = nullptr;
        free(draw_coords->tile_y);
        draw_coords->tile_y = nullptr;
    }

    if (draw_state != nullptr) {
        free(draw_state->draw_rr_node);
        draw_state->draw_rr_node = nullptr;
    }
#else
    ;
#endif /* NO_GRAPHICS */
}

void init_draw_coords(float width_val) {
#ifndef NO_GRAPHICS
    /* Load the arrays containing the left and bottom coordinates of the clbs   *
     * forming the FPGA.  tile_width_val sets the width and height of a drawn    *
     * clb.                                                                     */
    t_draw_state* draw_state = get_draw_state_vars();
    t_draw_coords* draw_coords = get_draw_coords_vars();
    auto& device_ctx = g_vpr_ctx.device();

    if (!draw_state->show_graphics && !draw_state->save_graphics
        && draw_state->graphics_commands.empty())
        return; //do not initialize only if --disp off and --save_graphics off
    /* Each time routing is on screen, need to reallocate the color of each *
     * rr_node, as the number of rr_nodes may change.						*/
    if (device_ctx.rr_nodes.size() != 0) {
        draw_state->draw_rr_node = (t_draw_rr_node*)vtr::realloc(
            draw_state->draw_rr_node,
            (device_ctx.rr_nodes.size()) * sizeof(t_draw_rr_node));
        for (size_t i = 0; i < device_ctx.rr_nodes.size(); i++) {
            draw_state->draw_rr_node[i].color = DEFAULT_RR_NODE_COLOR;
            draw_state->draw_rr_node[i].node_highlighted = false;
        }
    }
    draw_coords->tile_width = width_val;
    draw_coords->pin_size = 0.3;
    for (const auto& type : device_ctx.physical_tile_types) {
        auto num_pins = type.num_pins;
        if (num_pins > 0) {
            draw_coords->pin_size = std::min(draw_coords->pin_size,
                                             (draw_coords->get_tile_width() / (4.0F * num_pins)));
        }
    }

    size_t j = 0;
    for (size_t i = 0; i < (device_ctx.grid.width() - 1); i++) {
        draw_coords->tile_x[i] = (i * draw_coords->get_tile_width()) + j;
        j += device_ctx.chan_width.y_list[i] + 1; /* N wires need N+1 units of space */
    }
    draw_coords->tile_x[device_ctx.grid.width() - 1] = ((device_ctx.grid.width()
                                                         - 1)
                                                        * draw_coords->get_tile_width())
                                                       + j;
    j = 0;
    for (size_t i = 0; i < (device_ctx.grid.height() - 1); ++i) {
        draw_coords->tile_y[i] = (i * draw_coords->get_tile_width()) + j;
        j += device_ctx.chan_width.x_list[i] + 1;
    }
    draw_coords->tile_y[device_ctx.grid.height() - 1] = ((device_ctx.grid.height() - 1) * draw_coords->get_tile_width())
                                                        + j;
    /* Load coordinates of sub-blocks inside the clbs */
    draw_internal_init_blk();
    //Margin beyond edge of the drawn device to extend the visible world
    //Setting this to > 0.0 means 'Zoom Fit' leave some fraction of white
    //space around the device edges
    constexpr float VISIBLE_MARGIN = 0.01;

    float draw_width = draw_coords->tile_x[device_ctx.grid.width() - 1]
                       + draw_coords->get_tile_width();
    float draw_height = draw_coords->tile_y[device_ctx.grid.height() - 1]
                        + draw_coords->get_tile_width();

    initial_world = ezgl::rectangle(
        {-VISIBLE_MARGIN * draw_width, -VISIBLE_MARGIN * draw_height},
        {(1. + VISIBLE_MARGIN) * draw_width, (1. + VISIBLE_MARGIN)
                                                 * draw_height});
#else
    (void)width_val;
#endif /* NO_GRAPHICS */
}

#ifndef NO_GRAPHICS

/* Draws the blocks placed on the proper clbs.  Occupied blocks are darker colours *
 * while empty ones are lighter colours and have a dashed border.      */
static void drawplace(ezgl::renderer* g) {
    t_draw_state* draw_state = get_draw_state_vars();
    t_draw_coords* draw_coords = get_draw_coords_vars();
    auto& device_ctx = g_vpr_ctx.device();
    auto& cluster_ctx = g_vpr_ctx.clustering();
    auto& place_ctx = g_vpr_ctx.placement();

    ClusterBlockId bnum;
    int num_sub_tiles;

    g->set_line_width(0);
    for (size_t i = 0; i < device_ctx.grid.width(); i++) {
        for (size_t j = 0; j < device_ctx.grid.height(); j++) {
            /* Only the first block of a group should control drawing */
            if (device_ctx.grid[i][j].width_offset > 0
                || device_ctx.grid[i][j].height_offset > 0)
                continue;

            num_sub_tiles = device_ctx.grid[i][j].type->capacity;
            /* Don't draw if tile capacity is zero. eg-> corners. */
            if (num_sub_tiles == 0) {
                continue;
            }

            for (int k = 0; k < num_sub_tiles; ++k) {
                /* Look at the tile at start of large block */
                bnum = place_ctx.grid_blocks[i][j].blocks[k];
                /* Fill background for the clb. Do not fill if "show_blk_internal"
                 * is toggled.
                 */
                if (bnum == INVALID_BLOCK_ID)
                    continue;

                //Determine the block color and logical type
                ezgl::color block_color;
                t_logical_block_type_ptr logical_block_type = nullptr;

                //flag whether the current location is highlighted with a special color or not
                bool current_loc_is_highlighted = false;

                if (placer_breakpoint_reached())
                    current_loc_is_highlighted = highlight_loc_with_specific_color(int(i), int(j),
                                                                                   block_color);

                // No color specified at this location; use the block color.
                if (current_loc_is_highlighted == false) {
                    if (bnum != EMPTY_BLOCK_ID) {
                        block_color = draw_state->block_color(bnum);
                    } else {
                        block_color = get_block_type_color(
                            device_ctx.grid[i][j].type);
                        block_color = lighten_color(block_color,
                                                    EMPTY_BLOCK_LIGHTEN_FACTOR);
                    }
                }

                auto tile_type = device_ctx.grid[i][j].type;
                logical_block_type = pick_logical_type(tile_type);

                g->set_color(block_color);
                /* Get coords of current sub_tile */
                ezgl::rectangle abs_clb_bbox = draw_coords->get_absolute_clb_bbox(i, j, k,
                                                                                  logical_block_type);
                ezgl::point2d center = abs_clb_bbox.center();

                g->fill_rectangle(abs_clb_bbox);

                g->set_color(ezgl::BLACK);

                g->set_line_dash(
                    (EMPTY_BLOCK_ID == bnum) ? ezgl::line_dash::asymmetric_5_3 : ezgl::line_dash::none);
                if (draw_state->draw_block_outlines) {
                    g->draw_rectangle(abs_clb_bbox);
                }

                if (draw_state->draw_block_text) {
                    /* Draw text if the space has parts of the netlist */
                    if (bnum != EMPTY_BLOCK_ID && bnum != INVALID_BLOCK_ID) {
                        std::string name = cluster_ctx.clb_nlist.block_name(
                                               bnum)
                                           + vtr::string_fmt(" (#%zu)", size_t(bnum));

                        g->draw_text(center, name.c_str(), abs_clb_bbox.width(),
                                     abs_clb_bbox.height());
                    }
                    /* Draw text for block type so that user knows what block */
                    if (device_ctx.grid[i][j].width_offset == 0
                        && device_ctx.grid[i][j].height_offset == 0) {
                        std::string block_type_loc = device_ctx.grid[i][j].type->name;
                        block_type_loc += vtr::string_fmt(" (%d,%d)", i, j);

                        g->draw_text(
                            center
                                - ezgl::point2d(0,
                                                abs_clb_bbox.height() / 4),
                            block_type_loc.c_str(), abs_clb_bbox.width(),
                            abs_clb_bbox.height());
                    }
                }
            }
        }
    }
}

static void drawnets(ezgl::renderer* g) {
    t_draw_state* draw_state = get_draw_state_vars();
    t_draw_coords* draw_coords = get_draw_coords_vars();
    /* This routine draws the nets on the placement.  The nets have not *
     * yet been routed, so we just draw a chain showing a possible path *
     * for each net.  This gives some idea of future congestion.        */

    ClusterBlockId b1, b2;
    auto& cluster_ctx = g_vpr_ctx.clustering();

    float NET_ALPHA = draw_state->net_alpha;

    g->set_line_dash(ezgl::line_dash::none);
    g->set_line_width(0);

    /* Draw the net as a star from the source to each sink. Draw from centers of *
     * blocks (or sub blocks in the case of IOs).                                */

    for (auto net_id : cluster_ctx.clb_nlist.nets()) {
        if (cluster_ctx.clb_nlist.net_is_ignored(net_id))
            continue; /* Don't draw */

        g->set_color(draw_state->net_color[net_id],
                     draw_state->net_color[net_id].alpha * NET_ALPHA);
        b1 = cluster_ctx.clb_nlist.net_driver_block(net_id);
        ezgl::point2d driver_center = draw_coords->get_absolute_clb_bbox(b1,
                                                                         cluster_ctx.clb_nlist.block_type(b1))
                                          .center();
        for (auto pin_id : cluster_ctx.clb_nlist.net_sinks(net_id)) {
            b2 = cluster_ctx.clb_nlist.pin_block(pin_id);
            ezgl::point2d sink_center = draw_coords->get_absolute_clb_bbox(b2,
                                                                           cluster_ctx.clb_nlist.block_type(b2))
                                            .center();
            g->draw_line(driver_center, sink_center);
            /* Uncomment to draw a chain instead of a star. */
            /* driver_center = sink_center;  */
        }
    }
}

static void draw_congestion(ezgl::renderer* g) {
    /* Draws all the overused routing resources (i.e. congestion) in various contrasting colors showing congestion ratio.   */
    t_draw_state* draw_state = get_draw_state_vars();

    if (draw_state->show_congestion == DRAW_NO_CONGEST) {
        return;
    }

    auto& device_ctx = g_vpr_ctx.device();
    const auto& rr_graph = device_ctx.rr_graph;
    auto& route_ctx = g_vpr_ctx.routing();

    //Record min/max congestion
    float min_congestion_ratio = 1.;
    float max_congestion_ratio = min_congestion_ratio;
    std::vector<int> congested_rr_nodes = collect_congested_rr_nodes();
    for (int inode : congested_rr_nodes) {
        short occ = route_ctx.rr_node_route_inf[inode].occ();
        short capacity = rr_graph.node_capacity(RRNodeId(inode));

        float congestion_ratio = float(occ) / capacity;

        max_congestion_ratio = std::max(max_congestion_ratio, congestion_ratio);
    }

    char msg[vtr::bufsize];
    if (draw_state->show_congestion == DRAW_CONGESTED) {
        sprintf(msg, "RR Node Overuse ratio range (%.2f, %.2f]", min_congestion_ratio, max_congestion_ratio);
    } else {
        VTR_ASSERT(draw_state->show_congestion == DRAW_CONGESTED_WITH_NETS);
        sprintf(msg, "RR Node Overuse ratio range (%.2f, %.2f] (and congested nets)", min_congestion_ratio, max_congestion_ratio);
    }
    application.update_message(msg);

    std::shared_ptr<vtr::ColorMap> cmap = std::make_shared<vtr::PlasmaColorMap>(min_congestion_ratio, max_congestion_ratio);

    //Sort the nodes in ascending order of value for drawing, this ensures high
    //valued nodes are not overdrawn by lower value ones (e.g-> when zoomed-out far)
    auto cmp_ascending_acc_cost = [&](int lhs_node, int rhs_node) {
        short lhs_occ = route_ctx.rr_node_route_inf[lhs_node].occ();
        short lhs_capacity = rr_graph.node_capacity(RRNodeId(lhs_node));

        short rhs_occ = route_ctx.rr_node_route_inf[rhs_node].occ();
        short rhs_capacity = rr_graph.node_capacity(RRNodeId(rhs_node));

        float lhs_cong_ratio = float(lhs_occ) / lhs_capacity;
        float rhs_cong_ratio = float(rhs_occ) / rhs_capacity;

        return lhs_cong_ratio < rhs_cong_ratio;
    };
    std::sort(congested_rr_nodes.begin(), congested_rr_nodes.end(), cmp_ascending_acc_cost);

    if (draw_state->show_congestion == DRAW_CONGESTED_WITH_NETS) {
        auto rr_node_nets = collect_rr_node_nets();

        for (int inode : congested_rr_nodes) {
            for (ClusterNetId net : rr_node_nets[inode]) {
                ezgl::color color = kelly_max_contrast_colors[size_t(net) % kelly_max_contrast_colors.size()];
                draw_state->net_color[net] = color;
            }
        }
        g->set_line_width(0);
        drawroute(HIGHLIGHTED, g);

        //Reset colors
        for (int inode : congested_rr_nodes) {
            for (ClusterNetId net : rr_node_nets[inode]) {
                draw_state->net_color[net] = DEFAULT_RR_NODE_COLOR;
            }
        }
    } else {
        g->set_line_width(2);
    }

    //Draw each congested node
    for (int inode : congested_rr_nodes) {
        short occ = route_ctx.rr_node_route_inf[inode].occ();
        short capacity = rr_graph.node_capacity(RRNodeId(inode));

        float congestion_ratio = float(occ) / capacity;

        bool node_congested = (occ > capacity);
        VTR_ASSERT(node_congested);

        ezgl::color color = to_ezgl_color(cmap->color(congestion_ratio));

        switch (rr_graph.node_type(RRNodeId(inode))) {
            case CHANX: //fallthrough
            case CHANY:
                draw_rr_chan(inode, color, g);
                break;

            case IPIN: //fallthrough
            case OPIN:
                draw_rr_pin(inode, color, g);
                break;
            default:
                break;
        }
    }

    draw_state->color_map = std::move(cmap);
}

static void draw_routing_costs(ezgl::renderer* g) {
    /* Draws routing resource nodes colored according to their congestion costs */

    t_draw_state* draw_state = get_draw_state_vars();

    /* show_routing_costs controls whether the total/sum of the costs or individual
     * cost components (base cost, accumulated cost, present cost) are shown, and
     * whether colours are proportional to the node's cost or the logarithm of
     * it's cost.*/
    if (draw_state->show_routing_costs == DRAW_NO_ROUTING_COSTS) {
        return;
    }

    auto& device_ctx = g_vpr_ctx.device();
    auto& route_ctx = g_vpr_ctx.routing();
    g->set_line_width(0);

    VTR_ASSERT(!route_ctx.rr_node_route_inf.empty());

    float min_cost = std::numeric_limits<float>::infinity();
    float max_cost = -min_cost;
    std::vector<float> rr_node_costs(device_ctx.rr_nodes.size(), 0.);

    for (size_t inode = 0; inode < device_ctx.rr_nodes.size(); inode++) {
        float cost = 0.;
        if (draw_state->show_routing_costs == DRAW_TOTAL_ROUTING_COSTS
            || draw_state->show_routing_costs
                   == DRAW_LOG_TOTAL_ROUTING_COSTS) {
            cost = get_single_rr_cong_cost(inode,
                                           get_draw_state_vars()->pres_fac);

        } else if (draw_state->show_routing_costs == DRAW_BASE_ROUTING_COSTS) {
            cost = get_single_rr_cong_base_cost(inode);

        } else if (draw_state->show_routing_costs == DRAW_ACC_ROUTING_COSTS
                   || draw_state->show_routing_costs
                          == DRAW_LOG_ACC_ROUTING_COSTS) {
            cost = get_single_rr_cong_acc_cost(inode);

        } else {
            VTR_ASSERT(
                draw_state->show_routing_costs == DRAW_PRES_ROUTING_COSTS
                || draw_state->show_routing_costs
                       == DRAW_LOG_PRES_ROUTING_COSTS);
            cost = get_single_rr_cong_pres_cost(inode,
                                                get_draw_state_vars()->pres_fac);
        }

        if (draw_state->show_routing_costs == DRAW_LOG_TOTAL_ROUTING_COSTS
            || draw_state->show_routing_costs == DRAW_LOG_ACC_ROUTING_COSTS
            || draw_state->show_routing_costs
                   == DRAW_LOG_PRES_ROUTING_COSTS) {
            cost = std::log(cost);
        }
        rr_node_costs[inode] = cost;
        min_cost = std::min(min_cost, cost);
        max_cost = std::max(max_cost, cost);
    }

    //Hide min value, draw_rr_costs() ignores NaN's
    for (size_t inode = 0; inode < device_ctx.rr_nodes.size(); inode++) {
        if (rr_node_costs[inode] == min_cost) {
            rr_node_costs[inode] = NAN;
        }
    }
    char msg[vtr::bufsize];
    if (draw_state->show_routing_costs == DRAW_TOTAL_ROUTING_COSTS) {
        sprintf(msg, "Total Congestion Cost Range [%g, %g]", min_cost,
                max_cost);
    } else if (draw_state->show_routing_costs == DRAW_LOG_TOTAL_ROUTING_COSTS) {
        sprintf(msg, "Log Total Congestion Cost Range [%g, %g]", min_cost,
                max_cost);
    } else if (draw_state->show_routing_costs == DRAW_BASE_ROUTING_COSTS) {
        sprintf(msg, "Base Congestion Cost Range [%g, %g]", min_cost, max_cost);
    } else if (draw_state->show_routing_costs == DRAW_ACC_ROUTING_COSTS) {
        sprintf(msg, "Accumulated (Historical) Congestion Cost Range [%g, %g]",
                min_cost, max_cost);
    } else if (draw_state->show_routing_costs == DRAW_LOG_ACC_ROUTING_COSTS) {
        sprintf(msg,
                "Log Accumulated (Historical) Congestion Cost Range [%g, %g]",
                min_cost, max_cost);
    } else if (draw_state->show_routing_costs == DRAW_PRES_ROUTING_COSTS) {
        sprintf(msg, "Present Congestion Cost Range [%g, %g]", min_cost,
                max_cost);
    } else if (draw_state->show_routing_costs == DRAW_LOG_PRES_ROUTING_COSTS) {
        sprintf(msg, "Log Present Congestion Cost Range [%g, %g]", min_cost,
                max_cost);
    } else {
        sprintf(msg, "Cost Range [%g, %g]", min_cost, max_cost);
    }
    application.update_message(msg);

    draw_rr_costs(g, rr_node_costs, true);
}

static void draw_routing_bb(ezgl::renderer* g) {
    t_draw_state* draw_state = get_draw_state_vars();

    if (draw_state->show_routing_bb == OPEN) {
        return;
    }

    auto& route_ctx = g_vpr_ctx.routing();
    auto& cluster_ctx = g_vpr_ctx.clustering();

    VTR_ASSERT(draw_state->show_routing_bb != OPEN);
    VTR_ASSERT(draw_state->show_routing_bb < (int)route_ctx.route_bb.size());

    t_draw_coords* draw_coords = get_draw_coords_vars();

    auto net_id = ClusterNetId(draw_state->show_routing_bb);
    const t_bb* bb = &route_ctx.route_bb[net_id];

    //The router considers an RR node to be 'within' the the bounding box if it
    //is *loosely* greater (i.e. greater than or equal) the left/bottom edges, and
    //it is *loosely* less (i.e. less than or equal) the right/top edges.
    //
    //In the graphics we represent this by drawing the BB so that legal RR node start/end points
    //are contained within the drawn box. Since VPR associates each x/y channel location to
    //the right/top of the tile with the same x/y cordinates, this means we draw the box so that:
    //  * The left edge is to the left of the channel at bb xmin (including the channel at xmin)
    //  * The bottom edge is to the below of the channel at bb ymin (including the channel at ymin)
    //  * The right edge is to the right of the channel at bb xmax (including the channel at xmax)
    //  * The top edge is to the right of the channel at bb ymax (including the channel at ymax)
    //Since tile_x/tile_y correspond to the drawing coordinates the block at grid x/y's bottom-left corner
    //this means we need to shift the top/right drawn co-ordinate one tile + channel width right/up so
    //the drawn box contains the top/right channels
    double draw_xlow = draw_coords->tile_x[bb->xmin];
    double draw_ylow = draw_coords->tile_y[bb->ymin];
    double draw_xhigh = draw_coords->tile_x[bb->xmax]
                        + 2 * draw_coords->get_tile_width();
    double draw_yhigh = draw_coords->tile_y[bb->ymax]
                        + 2 * draw_coords->get_tile_height();

    g->set_color(blk_RED);
    g->draw_rectangle({draw_xlow, draw_ylow}, {draw_xhigh, draw_yhigh});

    ezgl::color fill = blk_SKYBLUE;
    fill.alpha *= 0.3;
    g->set_color(fill);
    g->fill_rectangle({draw_xlow, draw_ylow}, {draw_xhigh, draw_yhigh});

    draw_routed_net(net_id, g);

    std::string msg;
    msg += "Showing BB";
    msg += " (" + std::to_string(bb->xmin) + ", " + std::to_string(bb->ymin)
           + ", " + std::to_string(bb->xmax) + ", " + std::to_string(bb->ymax)
           + ")";
    msg += " and routing for net '" + cluster_ctx.clb_nlist.net_name(net_id)
           + "'";
    msg += " (#" + std::to_string(size_t(net_id)) + ")";
    application.update_message(msg.c_str());
}

void draw_rr(ezgl::renderer* g) {
    /* Draws the routing resources that exist in the FPGA, if the user wants *
     * them drawn.                                                           */
    t_draw_state* draw_state = get_draw_state_vars();
    auto& device_ctx = g_vpr_ctx.device();
    const auto& rr_graph = device_ctx.rr_graph;

    if (draw_state->draw_rr_toggle == DRAW_NO_RR) {
        g->set_line_width(3);
        drawroute(HIGHLIGHTED, g);
        g->set_line_width(0);
        return;
    }

    g->set_line_dash(ezgl::line_dash::none);

    for (size_t inode = 0; inode < device_ctx.rr_nodes.size(); inode++) {
        RRNodeId rr_node = RRNodeId(inode);
        if (!draw_state->draw_rr_node[inode].node_highlighted) {
            /* If not highlighted node, assign color based on type. */
            switch (rr_graph.node_type(rr_node)) {
                case CHANX:
                case CHANY:
                    draw_state->draw_rr_node[inode].color = DEFAULT_RR_NODE_COLOR;
                    break;
                case OPIN:
                    draw_state->draw_rr_node[inode].color = ezgl::PINK;
                    break;
                case IPIN:
                    draw_state->draw_rr_node[inode].color = blk_LIGHTSKYBLUE;
                    break;
                case SOURCE:
                    draw_state->draw_rr_node[inode].color = ezgl::PLUM;
                    break;
                case SINK:
                    draw_state->draw_rr_node[inode].color = ezgl::DARK_SLATE_BLUE;
                    break;
                default:
                    break;
            }
        }

        /* Now call drawing routines to draw the node. */
        switch (rr_graph.node_type(rr_node)) {
            case SINK:
                draw_rr_src_sink(inode, draw_state->draw_rr_node[inode].color, g);
                break;
            case SOURCE:
                draw_rr_edges(inode, g);
                draw_rr_src_sink(inode, draw_state->draw_rr_node[inode].color, g);
                break;

            case CHANX:
                draw_rr_chan(inode, draw_state->draw_rr_node[inode].color, g);
                draw_rr_edges(inode, g);
                break;

            case CHANY:
                draw_rr_chan(inode, draw_state->draw_rr_node[inode].color, g);
                draw_rr_edges(inode, g);
                break;

            case IPIN:
                draw_rr_pin(inode, draw_state->draw_rr_node[inode].color, g);
                draw_rr_edges(inode, g);
                break;

            case OPIN:
                draw_rr_pin(inode, draw_state->draw_rr_node[inode].color, g);
                draw_rr_edges(inode, g);
                break;

            default:
                vpr_throw(VPR_ERROR_OTHER, __FILE__, __LINE__,
                          "in draw_rr: Unexpected rr_node type: %d.\n", rr_graph.node_type(rr_node));
        }
    }

    drawroute(HIGHLIGHTED, g);
}

static void draw_rr_chan(int inode, const ezgl::color color, ezgl::renderer* g) {
    auto& device_ctx = g_vpr_ctx.device();
    const auto& rr_graph = device_ctx.rr_graph;
    auto rr_node = RRNodeId(inode);

    t_rr_type type = rr_graph.node_type(rr_node);

    VTR_ASSERT(type == CHANX || type == CHANY);

    ezgl::rectangle bound_box = draw_get_rr_chan_bbox(inode);
    Direction dir = rr_graph.node_direction(rr_node);

    //We assume increasing direction, and swap if needed
    ezgl::point2d start = bound_box.bottom_left();
    ezgl::point2d end = bound_box.top_right();
    if (dir == Direction::DEC) {
        std::swap(start, end);
    }

    g->set_color(color);
    if (color != DEFAULT_RR_NODE_COLOR) {
        // If wire is highlighted, then draw with thicker linewidth.
        g->set_line_width(3);
    }

    g->draw_line(start, end);

    if (color != DEFAULT_RR_NODE_COLOR) {
        // Revert width change
        g->set_line_width(0);
    }

    e_side mux_dir = TOP;
    int coord_min = -1;
    int coord_max = -1;
    if (type == CHANX) {
        coord_min = rr_graph.node_xlow(rr_node);
        coord_max = rr_graph.node_xhigh(rr_node);
        if (dir == Direction::INC) {
            mux_dir = RIGHT;
        } else {
            mux_dir = LEFT;
        }
    } else {
        VTR_ASSERT(type == CHANY);
        coord_min = rr_graph.node_ylow(rr_node);
        coord_max = rr_graph.node_yhigh(rr_node);
        if (dir == Direction::INC) {
            mux_dir = TOP;
        } else {
            mux_dir = BOTTOM;
        }
    }

    //Draw direction indicators at the boundary of each switch block, and label them
    //with the corresponding switch point (see build_switchblocks.c for a description of switch points)
    t_draw_coords* draw_coords = get_draw_coords_vars();
    float arrow_offset = DEFAULT_ARROW_SIZE / 2;
    ezgl::color arrow_color = blk_LIGHTGREY;
    ezgl::color text_color = ezgl::BLACK;
    for (int k = coord_min; k <= coord_max; ++k) {
        int switchpoint_min = -1;
        int switchpoint_max = -1;
        if (dir == Direction::INC) {
            switchpoint_min = k - coord_min;
            switchpoint_max = switchpoint_min + 1;
        } else {
            switchpoint_min = (coord_max + 1) - k;
            switchpoint_max = switchpoint_min - 1;
        }

        ezgl::point2d arrow_loc_min(0, 0);
        ezgl::point2d arrow_loc_max(0, 0);
        if (type == CHANX) {
            float sb_xmin = draw_coords->tile_x[k];
            arrow_loc_min = {sb_xmin + arrow_offset, start.y};

            float sb_xmax = draw_coords->tile_x[k] + draw_coords->get_tile_width();
            arrow_loc_max = {sb_xmax - arrow_offset, start.y};

        } else {
            float sb_ymin = draw_coords->tile_y[k];
            arrow_loc_min = {start.x, sb_ymin + arrow_offset};

            float sb_ymax = draw_coords->tile_y[k] + draw_coords->get_tile_height();
            arrow_loc_max = {start.x, sb_ymax - arrow_offset};
        }

        if (switchpoint_min == 0) {
            if (dir != Direction::BIDIR) {
                //Draw a mux at the start of each wire, labelled with it's size (#inputs)
                draw_mux_with_size(start, mux_dir, WIRE_DRAWING_WIDTH, rr_graph.node_fan_in(rr_node), g);
            }
        } else {
            //Draw arrows and label with switch point
            if (k == coord_min) {
                std::swap(arrow_color, text_color);
            }

            g->set_color(arrow_color);
            draw_triangle_along_line(g, arrow_loc_min, start, end);

            g->set_color(text_color);
            ezgl::rectangle bbox(ezgl::point2d(arrow_loc_min.x - DEFAULT_ARROW_SIZE / 2, arrow_loc_min.y - DEFAULT_ARROW_SIZE / 4),
                                 ezgl::point2d(arrow_loc_min.x + DEFAULT_ARROW_SIZE / 2, arrow_loc_min.y + DEFAULT_ARROW_SIZE / 4));
            ezgl::point2d center = bbox.center();
            g->draw_text(center, std::to_string(switchpoint_min), bbox.width(), bbox.height());

            if (k == coord_min) {
                //Revert
                std::swap(arrow_color, text_color);
            }
        }

        if (switchpoint_max == 0) {
            if (dir != Direction::BIDIR) {
                //Draw a mux at the start of each wire, labelled with it's size (#inputs)
                draw_mux_with_size(start, mux_dir, WIRE_DRAWING_WIDTH, rr_graph.node_fan_in(rr_node), g);
            }
        } else {
            //Draw arrows and label with switch point
            if (k == coord_max) {
                std::swap(arrow_color, text_color);
            }

            g->set_color(arrow_color);
            draw_triangle_along_line(g, arrow_loc_max, start, end);

            g->set_color(text_color);
            ezgl::rectangle bbox(ezgl::point2d(arrow_loc_max.x - DEFAULT_ARROW_SIZE / 2, arrow_loc_max.y - DEFAULT_ARROW_SIZE / 4),
                                 ezgl::point2d(arrow_loc_max.x + DEFAULT_ARROW_SIZE / 2, arrow_loc_max.y + DEFAULT_ARROW_SIZE / 4));
            ezgl::point2d center = bbox.center();
            g->draw_text(center, std::to_string(switchpoint_max), bbox.width(), bbox.height());

            if (k == coord_max) {
                //Revert
                std::swap(arrow_color, text_color);
            }
        }
    }
    g->set_color(color); //Ensure color is still set correctly if we drew any arrows/text
}

static void draw_rr_edges(int inode, ezgl::renderer* g) {
    /* Draws all the edges that the user wants shown between inode and what it *
     * connects to.  inode is assumed to be a CHANX, CHANY, or IPIN.           */
    t_draw_state* draw_state = get_draw_state_vars();
    auto& device_ctx = g_vpr_ctx.device();
    const auto& rr_graph = device_ctx.rr_graph;
    auto rr_node = RRNodeId(inode);

    t_rr_type from_type, to_type;
    int to_node, from_ptc_num, to_ptc_num;
    short switch_type;

    from_type = rr_graph.node_type(rr_node);

    if ((draw_state->draw_rr_toggle == DRAW_NODES_RR)
        || (draw_state->draw_rr_toggle == DRAW_NODES_SBOX_RR && (from_type == OPIN || from_type == SOURCE || from_type == IPIN))
        || (draw_state->draw_rr_toggle == DRAW_NODES_SBOX_CBOX_RR && (from_type == SOURCE || from_type == IPIN))) {
        return; /* Nothing to draw. */
    }

    from_ptc_num = rr_graph.node_ptc_num(RRNodeId(inode));

    for (t_edge_size iedge = 0, l = rr_graph.num_edges(RRNodeId(inode)); iedge < l; iedge++) {
        to_node = device_ctx.rr_nodes[inode].edge_sink_node(iedge);
        to_type = rr_graph.node_type(RRNodeId(to_node));
        to_ptc_num = rr_graph.node_ptc_num(RRNodeId(to_node));
        bool edge_configurable = device_ctx.rr_nodes[inode].edge_is_configurable(iedge);

        switch (from_type) {
            case OPIN:
                switch (to_type) {
                    case CHANX:
                    case CHANY:
                        if (draw_state->draw_rr_node[inode].color == ezgl::MAGENTA) {
                            // If OPIN was clicked on, set color to fan-out
                            ezgl::color color = draw_state->draw_rr_node[to_node].color;
                            g->set_color(color);
                        } else if (draw_state->draw_rr_node[to_node].color == ezgl::MAGENTA) {
                            // If CHANX or CHANY got clicked, set color to fan-in
                            ezgl::color color = draw_state->draw_rr_node[inode].color;
                            g->set_color(color);
                        } else {
                            g->set_color(ezgl::PINK);
                        }
                        draw_pin_to_chan_edge(inode, to_node, g);
                        break;
                    case IPIN:
                        if (draw_state->draw_rr_node[inode].color == ezgl::MAGENTA) {
                            ezgl::color color = draw_state->draw_rr_node[to_node].color;
                            g->set_color(color);
                        } else if (draw_state->draw_rr_node[to_node].color == ezgl::MAGENTA) {
                            ezgl::color color = draw_state->draw_rr_node[inode].color;
                            g->set_color(color);
                        } else {
                            g->set_color(ezgl::MEDIUM_PURPLE);
                        }
                        draw_pin_to_pin(inode, to_node, g);
                        break;
                    default:
                        vpr_throw(VPR_ERROR_OTHER, __FILE__, __LINE__,
                                  "in draw_rr_edges: node %d (type: %d) connects to node %d (type: %d).\n",
                                  inode, from_type, to_node, to_type);
                        break;
                }
                break;

            case CHANX: /* from_type */
                switch (to_type) {
                    case IPIN:
                        if (draw_state->draw_rr_toggle == DRAW_NODES_SBOX_RR) {
                            break;
                        }

                        if (draw_state->draw_rr_node[to_node].node_highlighted && draw_state->draw_rr_node[inode].color == DEFAULT_RR_NODE_COLOR) {
                            // If the IPIN is clicked on, draw connection to all the CHANX
                            // wire segments fanning into the pin. If a CHANX wire is clicked
                            // on, draw only the connection between that wire and the IPIN, with
                            // the pin fanning out from the wire.
                            break;
                        }

                        if (draw_state->draw_rr_node[inode].color == ezgl::MAGENTA) {
                            ezgl::color color = draw_state->draw_rr_node[to_node].color;
                            g->set_color(color);
                        } else if (draw_state->draw_rr_node[to_node].color == ezgl::MAGENTA) {
                            ezgl::color color = draw_state->draw_rr_node[inode].color;
                            g->set_color(color);
                        } else {
                            g->set_color(blk_LIGHTSKYBLUE);
                        }
                        draw_pin_to_chan_edge(to_node, inode, g);
                        break;

                    case CHANX:
                        if (draw_state->draw_rr_node[inode].color == ezgl::MAGENTA) {
                            ezgl::color color = draw_state->draw_rr_node[to_node].color;
                            g->set_color(color);
                        } else if (draw_state->draw_rr_node[to_node].color == ezgl::MAGENTA) {
                            ezgl::color color = draw_state->draw_rr_node[inode].color;
                            g->set_color(color);
                        } else if (!edge_configurable) {
                            ezgl::color color = blk_DARKGREY;
                            g->set_color(color);
                        } else {
                            g->set_color(blk_DARKGREEN);
                        }
                        switch_type = device_ctx.rr_nodes[inode].edge_switch(iedge);
                        draw_chanx_to_chanx_edge(RRNodeId(inode), RRNodeId(to_node),
                                                 to_ptc_num, switch_type, g);
                        break;

                    case CHANY:
                        if (draw_state->draw_rr_node[inode].color == ezgl::MAGENTA) {
                            ezgl::color color = draw_state->draw_rr_node[to_node].color;
                            g->set_color(color);
                        } else if (draw_state->draw_rr_node[to_node].color == ezgl::MAGENTA) {
                            ezgl::color color = draw_state->draw_rr_node[inode].color;
                            g->set_color(color);
                        } else if (!edge_configurable) {
                            g->set_color(blk_DARKGREY);
                        } else {
                            g->set_color(blk_DARKGREEN);
                        }
                        switch_type = device_ctx.rr_nodes[inode].edge_switch(iedge);
                        draw_chanx_to_chany_edge(inode, from_ptc_num, to_node,
                                                 to_ptc_num, FROM_X_TO_Y, switch_type, g);
                        break;

                    default:
                        vpr_throw(VPR_ERROR_OTHER, __FILE__, __LINE__,
                                  "in draw_rr_edges: node %d (type: %d) connects to node %d (type: %d).\n",
                                  inode, from_type, to_node, to_type);
                        break;
                }
                break;

            case CHANY: /* from_type */
                switch (to_type) {
                    case IPIN:
                        if (draw_state->draw_rr_toggle == DRAW_NODES_SBOX_RR) {
                            break;
                        }

                        if (draw_state->draw_rr_node[to_node].node_highlighted && draw_state->draw_rr_node[inode].color == DEFAULT_RR_NODE_COLOR) {
                            // If the IPIN is clicked on, draw connection to all the CHANY
                            // wire segments fanning into the pin. If a CHANY wire is clicked
                            // on, draw only the connection between that wire and the IPIN, with
                            // the pin fanning out from the wire.
                            break;
                        }

                        if (draw_state->draw_rr_node[inode].color == ezgl::MAGENTA) {
                            ezgl::color color = draw_state->draw_rr_node[to_node].color;
                            g->set_color(color);
                        } else if (draw_state->draw_rr_node[to_node].color == ezgl::MAGENTA) {
                            ezgl::color color = draw_state->draw_rr_node[inode].color;
                            g->set_color(color);
                        } else {
                            g->set_color(blk_LIGHTSKYBLUE);
                        }
                        draw_pin_to_chan_edge(to_node, inode, g);
                        break;

                    case CHANX:
                        if (draw_state->draw_rr_node[inode].color == ezgl::MAGENTA) {
                            ezgl::color color = draw_state->draw_rr_node[to_node].color;
                            g->set_color(color);
                        } else if (draw_state->draw_rr_node[to_node].color == ezgl::MAGENTA) {
                            ezgl::color color = draw_state->draw_rr_node[inode].color;
                            g->set_color(color);
                        } else if (!edge_configurable) {
                            ezgl::color color = blk_DARKGREY;
                            g->set_color(color);
                        } else {
                            g->set_color(blk_DARKGREEN);
                        }
                        switch_type = device_ctx.rr_nodes[inode].edge_switch(iedge);
                        draw_chanx_to_chany_edge(to_node, to_ptc_num, inode,
                                                 from_ptc_num, FROM_Y_TO_X, switch_type, g);
                        break;

                    case CHANY:
                        if (draw_state->draw_rr_node[inode].color == ezgl::MAGENTA) {
                            ezgl::color color = draw_state->draw_rr_node[to_node].color;
                            g->set_color(color);
                        } else if (draw_state->draw_rr_node[to_node].color == ezgl::MAGENTA) {
                            ezgl::color color = draw_state->draw_rr_node[inode].color;
                            g->set_color(color);
                        } else if (!edge_configurable) {
                            ezgl::color color = blk_DARKGREY;
                            g->set_color(color);
                        } else {
                            g->set_color(blk_DARKGREEN);
                        }
                        switch_type = device_ctx.rr_nodes[inode].edge_switch(iedge);
                        draw_chany_to_chany_edge(RRNodeId(inode), RRNodeId(to_node),
                                                 to_ptc_num, switch_type, g);
                        break;

                    default:
                        vpr_throw(VPR_ERROR_OTHER, __FILE__, __LINE__,
                                  "in draw_rr_edges: node %d (type: %d) connects to node %d (type: %d).\n",
                                  inode, from_type, to_node, to_type);
                        break;
                }
                break;
            case IPIN: // from_type
                switch (to_type) {
                    case SINK:
                        g->set_color(ezgl::DARK_SLATE_BLUE);
                        draw_pin_to_sink(inode, to_node, g);
                        break;

                    default:
                        vpr_throw(VPR_ERROR_OTHER, __FILE__, __LINE__,
                                  "in draw_rr_edges: node %d (type: %d) connects to node %d (type: %d).\n",
                                  inode, from_type, to_node, to_type);
                        break;
                }
                break;
            case SOURCE: // from_type
                switch (to_type) {
                    case OPIN:
                        g->set_color(ezgl::PLUM);
                        draw_source_to_pin(inode, to_node, g);
                        break;

                    default:
                        vpr_throw(VPR_ERROR_OTHER, __FILE__, __LINE__,
                                  "in draw_rr_edges: node %d (type: %d) connects to node %d (type: %d).\n",
                                  inode, from_type, to_node, to_type);
                        break;
                }
                break;
            default: /* from_type */
                vpr_throw(VPR_ERROR_OTHER, __FILE__, __LINE__,
                          "draw_rr_edges called with node %d of type %d.\n",
                          inode, from_type);
                break;
        }
    } /* End of for each edge loop */
}

static void draw_x(float x, float y, float size, ezgl::renderer* g) {
    /* Draws an X centered at (x,y).  The width and height of the X are each    *
     * 2 * size.                                                                */
    g->draw_line({x - size, y + size}, {x + size, y - size});
    g->draw_line({x - size, y - size}, {x + size, y + size});
}

static void draw_chanx_to_chany_edge(int chanx_node, int chanx_track, int chany_node, int chany_track, enum e_edge_dir edge_dir, short switch_type, ezgl::renderer* g) {
    t_draw_state* draw_state = get_draw_state_vars();
    t_draw_coords* draw_coords = get_draw_coords_vars();
    auto& device_ctx = g_vpr_ctx.device();
    const auto& rr_graph = device_ctx.rr_graph;

    /* Draws an edge (SBOX connection) between an x-directed channel and a    *
     * y-directed channel.                                                    */

    float x1, y1, x2, y2;
    ezgl::rectangle chanx_bbox;
    ezgl::rectangle chany_bbox;
    int chanx_xlow, chany_x, chany_ylow, chanx_y;

    /* Get the coordinates of the CHANX and CHANY segments. */
    chanx_bbox = draw_get_rr_chan_bbox(chanx_node);
    chany_bbox = draw_get_rr_chan_bbox(chany_node);

    /* (x1,y1): point on CHANX segment, (x2,y2): point on CHANY segment. */

    y1 = chanx_bbox.bottom();
    x2 = chany_bbox.left();

    chanx_xlow = rr_graph.node_xlow(RRNodeId(chanx_node));
    chanx_y = rr_graph.node_ylow(RRNodeId(chanx_node));
    chany_x = rr_graph.node_xlow(RRNodeId(chany_node));
    chany_ylow = rr_graph.node_ylow(RRNodeId(chany_node));

    if (chanx_xlow <= chany_x) { /* Can draw connection going right */
        /* Connection not at end of the CHANX segment. */
        x1 = draw_coords->tile_x[chany_x] + draw_coords->get_tile_width();

        if (rr_graph.node_direction(RRNodeId(chanx_node)) != Direction::BIDIR) {
            if (edge_dir == FROM_X_TO_Y) {
                if ((chanx_track % 2) == 1) { /* If dec wire, then going left */
                    x1 = draw_coords->tile_x[chany_x + 1];
                }
            }
        }

    } else { /* Must draw connection going left. */
        x1 = chanx_bbox.left();
    }

    if (chany_ylow <= chanx_y) { /* Can draw connection going up. */
        /* Connection not at end of the CHANY segment. */
        y2 = draw_coords->tile_y[chanx_y] + draw_coords->get_tile_width();

        if (rr_graph.node_direction(RRNodeId(chany_node)) != Direction::BIDIR) {
            if (edge_dir == FROM_Y_TO_X) {
                if ((chany_track % 2) == 1) { /* If dec wire, then going down */
                    y2 = draw_coords->tile_y[chanx_y + 1];
                }
            }
        }

    } else { /* Must draw connection going down. */
        y2 = chany_bbox.bottom();
    }

    g->draw_line({x1, y1}, {x2, y2});

    if (draw_state->draw_rr_toggle == DRAW_ALL_RR
        || draw_state->draw_rr_node[chanx_node].node_highlighted) {
        if (edge_dir == FROM_X_TO_Y) {
            draw_rr_switch(x1, y1, x2, y2,
                           device_ctx.rr_switch_inf[switch_type].buffered(),
                           device_ctx.rr_switch_inf[switch_type].configurable(), g);
        } else {
            draw_rr_switch(x2, y2, x1, y1,
                           device_ctx.rr_switch_inf[switch_type].buffered(),
                           device_ctx.rr_switch_inf[switch_type].configurable(), g);
        }
    }
}

static void draw_chanx_to_chanx_edge(RRNodeId from_node, RRNodeId to_node, int to_track, short switch_type, ezgl::renderer* g) {
    /* Draws a connection between two x-channel segments.  Passing in the track *
     * numbers allows this routine to be used for both rr_graph and routing     *
     * drawing->                                                                 */

    t_draw_state* draw_state = get_draw_state_vars();
    t_draw_coords* draw_coords = get_draw_coords_vars();
    auto& device_ctx = g_vpr_ctx.device();
    const auto& rr_graph = device_ctx.rr_graph;

    float x1, x2, y1, y2;
    ezgl::rectangle from_chan;
    ezgl::rectangle to_chan;
    int from_xlow, to_xlow, from_xhigh, to_xhigh;

    // Get the coordinates of the channel wires.
    from_chan = draw_get_rr_chan_bbox(size_t(from_node));
    to_chan = draw_get_rr_chan_bbox(size_t(to_node));

    /* (x1, y1) point on from_node, (x2, y2) point on to_node. */

    y1 = from_chan.bottom();
    y2 = to_chan.bottom();

    from_xlow = rr_graph.node_xlow(from_node);
    from_xhigh = rr_graph.node_xhigh(from_node);
    to_xlow = rr_graph.node_xlow(to_node);
    to_xhigh = rr_graph.node_xhigh(to_node);
    if (to_xhigh < from_xlow) { /* From right to left */
        /* UDSD Note by WMF: could never happen for INC wires, unless U-turn. For DEC
         * wires this handles well */
        x1 = from_chan.left();
        x2 = to_chan.right();
    } else if (to_xlow > from_xhigh) { /* From left to right */
        /* UDSD Note by WMF: could never happen for DEC wires, unless U-turn. For INC
         * wires this handles well */
        x1 = from_chan.right();
        x2 = to_chan.left();
    }

    /* Segments overlap in the channel.  Figure out best way to draw.  Have to  *
     * make sure the drawing is symmetric in the from rr and to rr so the edges *
     * will be drawn on top of each other for bidirectional connections.        */

    else {
        if (rr_graph.node_direction(to_node) != Direction::BIDIR) {
            /* must connect to to_node's wire beginning at x2 */
            if (to_track % 2 == 0) { /* INC wire starts at leftmost edge */
                VTR_ASSERT(from_xlow < to_xlow);
                x2 = to_chan.left();
                /* since no U-turns from_track must be INC as well */
                x1 = draw_coords->tile_x[to_xlow - 1]
                     + draw_coords->get_tile_width();
            } else { /* DEC wire starts at rightmost edge */
                VTR_ASSERT(from_xhigh > to_xhigh);
                x2 = to_chan.right();
                x1 = draw_coords->tile_x[to_xhigh + 1];
            }
        } else {
            if (to_xlow < from_xlow) { /* Draw from left edge of one to other */
                x1 = from_chan.left();
                x2 = draw_coords->tile_x[from_xlow - 1]
                     + draw_coords->get_tile_width();
            } else if (from_xlow < to_xlow) {
                x1 = draw_coords->tile_x[to_xlow - 1]
                     + draw_coords->get_tile_width();
                x2 = to_chan.left();

            }                                 /* The following then is executed when from_xlow == to_xlow */
            else if (to_xhigh > from_xhigh) { /* Draw from right edge of one to other */
                x1 = from_chan.right();
                x2 = draw_coords->tile_x[from_xhigh + 1];
            } else if (from_xhigh > to_xhigh) {
                x1 = draw_coords->tile_x[to_xhigh + 1];
                x2 = to_chan.right();
            } else { /* Complete overlap: start and end both align. Draw outside the sbox */
                x1 = from_chan.left();
                x2 = from_chan.left() + draw_coords->get_tile_width();
            }
        }
    }

    g->draw_line({x1, y1}, {x2, y2});

    if (draw_state->draw_rr_toggle == DRAW_ALL_RR
        || draw_state->draw_rr_node[size_t(from_node)].node_highlighted) {
        draw_rr_switch(x1, y1, x2, y2,
                       device_ctx.rr_switch_inf[switch_type].buffered(),
                       device_ctx.rr_switch_inf[switch_type].configurable(), g);
    }
}

static void draw_chany_to_chany_edge(RRNodeId from_node, RRNodeId to_node, int to_track, short switch_type, ezgl::renderer* g) {
    t_draw_state* draw_state = get_draw_state_vars();
    t_draw_coords* draw_coords = get_draw_coords_vars();
    auto& device_ctx = g_vpr_ctx.device();
    const auto& rr_graph = device_ctx.rr_graph;

    /* Draws a connection between two y-channel segments.  Passing in the track *
     * numbers allows this routine to be used for both rr_graph and routing     *
     * drawing->                                                                 */

    float x1, x2, y1, y2;
    ezgl::rectangle from_chan;
    ezgl::rectangle to_chan;
    int from_ylow, to_ylow, from_yhigh, to_yhigh; //, from_x, to_x;

    // Get the coordinates of the channel wires.
    from_chan = draw_get_rr_chan_bbox(size_t(from_node));
    to_chan = draw_get_rr_chan_bbox(size_t(to_node));

    // from_x = rr_graph.node_xlow(RRNodeId(from_node));
    // to_x = rr_graph.node_xlow(RRNodeId(to_node));
    from_ylow = rr_graph.node_ylow(from_node);
    from_yhigh = rr_graph.node_yhigh(from_node);
    to_ylow = rr_graph.node_ylow(to_node);
    to_yhigh = rr_graph.node_yhigh(to_node);

    /* (x1, y1) point on from_node, (x2, y2) point on to_node. */

    x1 = from_chan.left();
    x2 = to_chan.left();

    if (to_yhigh < from_ylow) { /* From upper to lower */
        y1 = from_chan.bottom();
        y2 = to_chan.top();
    } else if (to_ylow > from_yhigh) { /* From lower to upper */
        y1 = from_chan.top();
        y2 = to_chan.bottom();
    }

    /* Segments overlap in the channel.  Figure out best way to draw.  Have to  *
     * make sure the drawing is symmetric in the from rr and to rr so the edges *
     * will be drawn on top of each other for bidirectional connections.        */

    /* UDSD Modification by WMF Begin */
    else {
        if (rr_graph.node_direction(to_node) != Direction::BIDIR) {
            if (to_track % 2 == 0) { /* INC wire starts at bottom edge */

                y2 = to_chan.bottom();
                /* since no U-turns from_track must be INC as well */
                y1 = draw_coords->tile_y[to_ylow - 1]
                     + draw_coords->get_tile_width();
            } else { /* DEC wire starts at top edge */

                y2 = to_chan.top();
                y1 = draw_coords->tile_y[to_yhigh + 1];
            }
        } else {
            if (to_ylow < from_ylow) { /* Draw from bottom edge of one to other. */
                y1 = from_chan.bottom();
                y2 = draw_coords->tile_y[from_ylow - 1]
                     + draw_coords->get_tile_width();
            } else if (from_ylow < to_ylow) {
                y1 = draw_coords->tile_y[to_ylow - 1]
                     + draw_coords->get_tile_width();
                y2 = to_chan.bottom();
            } else if (to_yhigh > from_yhigh) { /* Draw from top edge of one to other. */
                y1 = from_chan.top();
                y2 = draw_coords->tile_y[from_yhigh + 1];
            } else if (from_yhigh > to_yhigh) {
                y1 = draw_coords->tile_y[to_yhigh + 1];
                y2 = to_chan.top();
            } else { /* Complete overlap: start and end both align. Draw outside the sbox */
                y1 = from_chan.bottom();
                y2 = from_chan.bottom() + draw_coords->get_tile_width();
            }
        }
    }

    /* UDSD Modification by WMF End */
    g->draw_line({x1, y1}, {x2, y2});

    if (draw_state->draw_rr_toggle == DRAW_ALL_RR
        || draw_state->draw_rr_node[size_t(from_node)].node_highlighted) {
        draw_rr_switch(x1, y1, x2, y2,
                       device_ctx.rr_switch_inf[switch_type].buffered(),
                       device_ctx.rr_switch_inf[switch_type].configurable(), g);
    }
}

/* This function computes and returns the boundary coordinates of a channel
 * wire segment. This can be used for drawing a wire or determining if a
 * wire has been clicked on by the user.
 * TODO: Fix this for global routing, currently for detailed only.
 */
ezgl::rectangle draw_get_rr_chan_bbox(int inode) {
    double left = 0, right = 0, top = 0, bottom = 0;
    t_draw_coords* draw_coords = get_draw_coords_vars();
    auto& device_ctx = g_vpr_ctx.device();
    const auto& rr_graph = device_ctx.rr_graph;
    auto rr_node = RRNodeId(inode);

    switch (rr_graph.node_type(rr_node)) {
        case CHANX:
            left = draw_coords->tile_x[rr_graph.node_xlow(rr_node)];
            right = draw_coords->tile_x[rr_graph.node_xhigh(rr_node)]
                    + draw_coords->get_tile_width();
            bottom = draw_coords->tile_y[rr_graph.node_ylow(rr_node)]
                     + draw_coords->get_tile_width()
                     + (1. + rr_graph.node_track_num(rr_node));
            top = draw_coords->tile_y[rr_graph.node_ylow(rr_node)]
                  + draw_coords->get_tile_width()
                  + (1. + rr_graph.node_track_num(rr_node));
            break;
        case CHANY:
            left = draw_coords->tile_x[rr_graph.node_xlow(rr_node)]
                   + draw_coords->get_tile_width()
                   + (1. + rr_graph.node_track_num(rr_node));
            right = draw_coords->tile_x[rr_graph.node_xlow(rr_node)]
                    + draw_coords->get_tile_width()
                    + (1. + rr_graph.node_track_num(rr_node));
            bottom = draw_coords->tile_y[rr_graph.node_ylow(rr_node)];
            top = draw_coords->tile_y[rr_graph.node_yhigh(rr_node)]
                  + draw_coords->get_tile_width();
            break;
        default:
            // a problem. leave at default value (ie. zeros)
            break;
    }
    ezgl::rectangle bound_box({left, bottom}, {right, top});

    return bound_box;
}

static void draw_rr_switch(float from_x, float from_y, float to_x, float to_y, bool buffered, bool configurable, ezgl::renderer* g) {
    /* Draws a buffer (triangle) or pass transistor (circle) on the edge        *
     * connecting from to to, depending on the status of buffered.  The drawing *
     * is closest to the from_node, since it reflects the switch type of from.  */

    if (!buffered) {
        if (configurable) { /* Draw a circle for a pass transistor */
            float xcen = from_x + (to_x - from_x) / 10.;
            float ycen = from_y + (to_y - from_y) / 10.;
            const float switch_rad = 0.15;
            g->draw_arc({xcen, ycen}, switch_rad, 0., 360.);
        } else {
            //Pass, nothing to draw
        }
    } else { /* Buffer */
        if (from_x == to_x || from_y == to_y) {
            //Straight connection
            draw_triangle_along_line(g, {from_x, from_y}, {to_x, to_y},
                                     SB_EDGE_STRAIGHT_ARROW_POSITION);
        } else {
            //Turn connection
            draw_triangle_along_line(g, {from_x, from_y}, {to_x, to_y},
                                     SB_EDGE_TURN_ARROW_POSITION);
        }
    }
}

static void draw_rr_pin(int inode, const ezgl::color& color, ezgl::renderer* g) {
    /* Draws an IPIN or OPIN rr_node.  Note that the pin can appear on more    *
     * than one side of a clb.  Also note that this routine can change the     *
     * current color to BLACK.                                                 */

    t_draw_coords* draw_coords = get_draw_coords_vars();

    float xcen, ycen;
    char str[vtr::bufsize];
    auto& device_ctx = g_vpr_ctx.device();
    const auto& rr_graph = device_ctx.rr_graph;

    int ipin = rr_graph.node_pin_num(RRNodeId(inode));

    g->set_color(color);

    /* TODO: This is where we can hide fringe physical pins and also identify globals (hide, color, show) */
    /* As nodes may appear on more than one side, walk through the possible nodes
     * - draw the pin on each side that it appears
     */
    for (const e_side& pin_side : SIDES) {
        if (!rr_graph.is_node_on_specific_side(RRNodeId(inode), pin_side)) {
            continue;
        }
        draw_get_rr_pin_coords(inode, &xcen, &ycen, pin_side);
        g->fill_rectangle(
            {xcen - draw_coords->pin_size, ycen - draw_coords->pin_size},
            {xcen + draw_coords->pin_size, ycen + draw_coords->pin_size});
        sprintf(str, "%d", ipin);
        g->set_color(ezgl::BLACK);
        g->draw_text({xcen, ycen}, str, 2 * draw_coords->pin_size,
                     2 * draw_coords->pin_size);
        g->set_color(color);
    }
}

/* Returns the coordinates at which the center of this pin should be drawn. *
 * inode gives the node number, and iside gives the side of the clb or pad  *
 * the physical pin is on.                                                  */
void draw_get_rr_pin_coords(int inode, float* xcen, float* ycen, const e_side& pin_side) {
    auto& device_ctx = g_vpr_ctx.device();
    draw_get_rr_pin_coords(device_ctx.rr_nodes[inode], xcen, ycen, pin_side);
}

void draw_get_rr_pin_coords(const t_rr_node& node, float* xcen, float* ycen, const e_side& pin_side) {
    t_draw_coords* draw_coords = get_draw_coords_vars();

    int i, j, k, ipin, pins_per_sub_tile;
    float offset, xc, yc, step;
    t_physical_tile_type_ptr type;
    auto& device_ctx = g_vpr_ctx.device();
    const auto& rr_graph = device_ctx.rr_graph;
    auto rr_node = node.id();

    i = rr_graph.node_xlow(rr_node);
    j = rr_graph.node_ylow(rr_node);

    xc = draw_coords->tile_x[i];
    yc = draw_coords->tile_y[j];

    ipin = rr_graph.node_pin_num(rr_node);
    type = device_ctx.grid[i][j].type;
    pins_per_sub_tile = type->num_pins / type->capacity;
    k = ipin / pins_per_sub_tile;

    /* Since pins numbers go across all sub_tiles in a block in order
     * we can treat as a block box for this step */

    /* For each sub_tile we need and extra padding space */
    step = (float)(draw_coords->get_tile_width())
           / (float)(type->num_pins + type->capacity);
    offset = (ipin + k + 1) * step;

    switch (pin_side) {
        case LEFT:
            yc += offset;
            break;

        case RIGHT:
            xc += draw_coords->get_tile_width();
            yc += offset;
            break;

        case BOTTOM:
            xc += offset;
            break;

        case TOP:
            xc += offset;
            yc += draw_coords->get_tile_width();
            break;

        default:
            vpr_throw(VPR_ERROR_OTHER, __FILE__, __LINE__,
                      "in draw_get_rr_pin_coords: Unexpected side %s.\n",
                      SIDE_STRING[pin_side]);
            break;
    }

    *xcen = xc;
    *ycen = yc;
}

static void draw_rr_src_sink(int inode, ezgl::color color, ezgl::renderer* g) {
    t_draw_coords* draw_coords = get_draw_coords_vars();

    auto& device_ctx = g_vpr_ctx.device();
    const auto& rr_graph = device_ctx.rr_graph;

    float xcen, ycen;
    draw_get_rr_src_sink_coords(device_ctx.rr_nodes[inode], &xcen, &ycen);

    g->set_color(color);

    g->fill_rectangle(
        {xcen - draw_coords->pin_size, ycen - draw_coords->pin_size},
        {xcen + draw_coords->pin_size, ycen + draw_coords->pin_size});

    std::string str = vtr::string_fmt("%d",
                                      rr_graph.node_class_num(RRNodeId(inode)));
    g->set_color(ezgl::BLACK);
    g->draw_text({xcen, ycen}, str.c_str(), 2 * draw_coords->pin_size,
                 2 * draw_coords->pin_size);
    g->set_color(color);
}

static void draw_get_rr_src_sink_coords(const t_rr_node& node, float* xcen, float* ycen) {
    t_draw_coords* draw_coords = get_draw_coords_vars();

    auto& device_ctx = g_vpr_ctx.device();
    const auto& rr_graph = device_ctx.rr_graph;
    RRNodeId rr_node = node.id();
    t_physical_tile_type_ptr tile_type = device_ctx.grid[rr_graph.node_xlow(rr_node)][rr_graph.node_ylow(rr_node)].type;

    //Number of classes (i.e. src/sinks) we need to draw
    float num_class = tile_type->class_inf.size();

    int height = tile_type->height; //Height in blocks

    //How many classes to draw per unit block height
    int class_per_height = num_class;
    if (height > 1) {
        class_per_height = num_class / (height - 1);
    }

    int class_height_offset = rr_graph.node_class_num(rr_node) / class_per_height; //Offset wrt block height
    int class_height_shift = rr_graph.node_class_num(rr_node) % class_per_height;  //Offset within unit block

    float xc = draw_coords->tile_x[rr_graph.node_xlow(rr_node)];
    float yc = draw_coords->tile_y[rr_graph.node_ylow(rr_node) + class_height_offset];

    *xcen = xc + 0.5 * draw_coords->get_tile_width();

    float class_section_height = class_per_height + 1;

    float ypos = (class_height_shift + 1) / class_section_height;
    *ycen = yc + ypos * draw_coords->get_tile_height();
}

/* Draws the nets in the positions fixed by the router.  If draw_net_type is *
 * ALL_NETS, draw all the nets.  If it is HIGHLIGHTED, draw only the nets    *
 * that are not coloured black (useful for drawing over the rr_graph).       */

static void drawroute(enum e_draw_net_type draw_net_type, ezgl::renderer* g) {
    /* Next free track in each channel segment if routing is GLOBAL */

    auto& cluster_ctx = g_vpr_ctx.clustering();

    t_draw_state* draw_state = get_draw_state_vars();

    float NET_ALPHA = draw_state->net_alpha;

    g->set_line_dash(ezgl::line_dash::none);
    g->set_color(ezgl::BLACK, ezgl::BLACK.alpha * NET_ALPHA);

    /* Now draw each net, one by one.      */

    for (auto net_id : cluster_ctx.clb_nlist.nets()) {
        if (draw_net_type == HIGHLIGHTED
            && draw_state->net_color[net_id] == ezgl::BLACK)
            continue;

        draw_routed_net(net_id, g);
    } /* End for (each net) */
}

static void draw_routed_net(ClusterNetId net_id, ezgl::renderer* g) {
    auto& route_ctx = g_vpr_ctx.routing();
    auto& cluster_ctx = g_vpr_ctx.clustering();

    t_draw_state* draw_state = get_draw_state_vars();

    if (cluster_ctx.clb_nlist.net_is_ignored(net_id)) /* Don't draw. */
        return;

    if (route_ctx.trace[net_id].head == nullptr) /* No routing->  Skip.  (Allows me to draw */
        return;                                  /* partially complete routes).            */

    t_trace* tptr = route_ctx.trace[net_id].head; /* SOURCE to start */
    int inode = tptr->index;

    std::vector<int> rr_nodes_to_draw;
    rr_nodes_to_draw.push_back(inode);
    for (;;) {
        tptr = tptr->next;
        inode = tptr->index;

        if (draw_if_net_highlighted(net_id)) {
            /* If a net has been highlighted, highlight the whole net in *
             * the same color.											 */
            draw_state->draw_rr_node[inode].color = draw_state->net_color[net_id];
            draw_state->draw_rr_node[inode].node_highlighted = true;
        } else {
            /* If not highlighted, draw the node in default color. */
            draw_state->draw_rr_node[inode].color = DEFAULT_RR_NODE_COLOR;
        }

        rr_nodes_to_draw.push_back(inode);

        if (tptr->iswitch == OPEN) { //End of branch
            draw_partial_route(rr_nodes_to_draw, g);
            rr_nodes_to_draw.clear();

            /* Skip the next segment */
            tptr = tptr->next;
            if (tptr == nullptr)
                break;
            inode = tptr->index;
            rr_nodes_to_draw.push_back(inode);
        }

    } /* End loop over traceback. */

    draw_partial_route(rr_nodes_to_draw, g);
}

//Draws the set of rr_nodes specified, using the colors set in draw_state
void draw_partial_route(const std::vector<int>& rr_nodes_to_draw, ezgl::renderer* g) {
    t_draw_state* draw_state = get_draw_state_vars();
    auto& device_ctx = g_vpr_ctx.device();
    const auto& rr_graph = device_ctx.rr_graph;

    static vtr::OffsetMatrix<int> chanx_track; /* [1..device_ctx.grid.width() - 2][0..device_ctx.grid.height() - 2] */
    static vtr::OffsetMatrix<int> chany_track; /* [0..device_ctx.grid.width() - 2][1..device_ctx.grid.height() - 2] */
    if (draw_state->draw_route_type == GLOBAL) {
        /* Allocate some temporary storage if it's not already available. */
        size_t width = device_ctx.grid.width();
        size_t height = device_ctx.grid.height();
        if (chanx_track.empty()) {
            chanx_track = vtr::OffsetMatrix<int>({{{1, width - 1}, {0, height - 1}}});
        }

        if (chany_track.empty()) {
            chany_track = vtr::OffsetMatrix<int>({{{0, width - 1}, {1, height - 1}}});
        }

        for (size_t i = 1; i < width - 1; i++)
            for (size_t j = 0; j < height - 1; j++)
                chanx_track[i][j] = (-1);

        for (size_t i = 0; i < width - 1; i++)
            for (size_t j = 1; j < height - 1; j++)
                chany_track[i][j] = (-1);
    }

    for (size_t i = 1; i < rr_nodes_to_draw.size(); ++i) {
        int inode = rr_nodes_to_draw[i];
        RRNodeId rr_node = RRNodeId(inode);
        auto rr_type = rr_graph.node_type(rr_node);

        int prev_node = rr_nodes_to_draw[i - 1];
        auto prev_type = rr_graph.node_type(RRNodeId(prev_node));

        auto iedge = find_edge(prev_node, inode);
        auto switch_type = device_ctx.rr_nodes[prev_node].edge_switch(iedge);

        switch (rr_type) {
            case OPIN: {
                draw_rr_pin(inode, draw_state->draw_rr_node[inode].color, g);
                break;
            }
            case IPIN: {
                draw_rr_pin(inode, draw_state->draw_rr_node[inode].color, g);
                if (rr_graph.node_type(RRNodeId(prev_node)) == OPIN) {
                    draw_pin_to_pin(prev_node, inode, g);
                } else {
                    draw_pin_to_chan_edge(inode, prev_node, g);
                }
                break;
            }
            case CHANX: {
                if (draw_state->draw_route_type == GLOBAL)
                    chanx_track[rr_graph.node_xlow(rr_node)][rr_graph.node_ylow(rr_node)]++;

                int itrack = get_track_num(inode, chanx_track, chany_track);
                draw_rr_chan(inode, draw_state->draw_rr_node[inode].color, g);

                switch (prev_type) {
                    case CHANX: {
                        draw_chanx_to_chanx_edge(RRNodeId(prev_node), RRNodeId(inode),
                                                 itrack, switch_type, g);
                        break;
                    }
                    case CHANY: {
                        int prev_track = get_track_num(prev_node, chanx_track,
                                                       chany_track);
                        draw_chanx_to_chany_edge(inode, itrack, prev_node,

                                                 prev_track, FROM_Y_TO_X, switch_type, g);
                        break;
                    }
                    case OPIN: {
                        draw_pin_to_chan_edge(prev_node, inode, g);
                        break;
                    }
                    default: {
                        VPR_ERROR(VPR_ERROR_OTHER,
                                  "Unexpected connection from an rr_node of type %d to one of type %d.\n",
                                  prev_type, rr_type);
                    }
                }

                break;
            }
            case CHANY: {
                if (draw_state->draw_route_type == GLOBAL)
                    chany_track[rr_graph.node_xlow(rr_node)][rr_graph.node_ylow(rr_node)]++;

                int itrack = get_track_num(inode, chanx_track, chany_track);
                draw_rr_chan(inode, draw_state->draw_rr_node[inode].color, g);

                switch (prev_type) {
                    case CHANX: {
                        int prev_track = get_track_num(prev_node, chanx_track,
                                                       chany_track);
                        draw_chanx_to_chany_edge(prev_node, prev_track, inode,
                                                 itrack, FROM_X_TO_Y, switch_type, g);
                        break;
                    }
                    case CHANY: {
                        draw_chany_to_chany_edge(RRNodeId(prev_node), RRNodeId(inode),
                                                 itrack, switch_type, g);
                        break;
                    }
                    case OPIN: {
                        draw_pin_to_chan_edge(prev_node, inode, g);

                        break;
                    }
                    default: {
                        VPR_ERROR(VPR_ERROR_OTHER,
                                  "Unexpected connection from an rr_node of type %d to one of type %d.\n",
                                  prev_type, rr_type);
                    }
                }

                break;
            }
            default: {
                break;
            }
        }
    }
}

static int get_track_num(int inode, const vtr::OffsetMatrix<int>& chanx_track, const vtr::OffsetMatrix<int>& chany_track) {
    /* Returns the track number of this routing resource node.   */

    int i, j;
    t_rr_type rr_type;
    auto& device_ctx = g_vpr_ctx.device();
    const auto& rr_graph = device_ctx.rr_graph;
    RRNodeId rr_node = RRNodeId(inode);

    if (get_draw_state_vars()->draw_route_type == DETAILED)
        return (rr_graph.node_track_num(rr_node));

    /* GLOBAL route stuff below. */

    rr_type = rr_graph.node_type(rr_node);
    i = rr_graph.node_xlow(rr_node); /* NB: Global rr graphs must have only unit */
    j = rr_graph.node_ylow(rr_node); /* length channel segments.                 */

    switch (rr_type) {
        case CHANX:
            return (chanx_track[i][j]);

        case CHANY:
            return (chany_track[i][j]);

        default:
            vpr_throw(VPR_ERROR_OTHER, __FILE__, __LINE__,
                      "in get_track_num: Unexpected node type %d for node %d.\n",
                      rr_type, inode);
            return OPEN;
    }
}

/* This helper function determines whether a net has been highlighted. The highlighting
 * could be caused by the user clicking on a routing resource, toggled, or
 * fan-in/fan-out of a highlighted node.
 */
static bool draw_if_net_highlighted(ClusterNetId inet) {
    t_draw_state* draw_state = get_draw_state_vars();

    if (draw_state->net_color[inet] != DEFAULT_RR_NODE_COLOR) {
        return true;
    }

    return false;
}

/* If an rr_node has been clicked on, it will be highlighted in MAGENTA.
 * If so, and toggle nets is selected, highlight the whole net in that colour.
 */
void highlight_nets(char* message, int hit_node) {
    t_trace* tptr;
    auto& cluster_ctx = g_vpr_ctx.clustering();
    auto& route_ctx = g_vpr_ctx.routing();

    t_draw_state* draw_state = get_draw_state_vars();

    for (auto net_id : cluster_ctx.clb_nlist.nets()) {
        for (tptr = route_ctx.trace[net_id].head; tptr != nullptr;
             tptr = tptr->next) {
            if (draw_state->draw_rr_node[tptr->index].color == ezgl::MAGENTA) {
                draw_state->net_color[net_id] = draw_state->draw_rr_node[tptr->index].color;
                if (tptr->index == hit_node) {
                    std::string orig_msg(message);
                    sprintf(message, "%s  ||  Net: %zu (%s)", orig_msg.c_str(),
                            size_t(net_id),
                            cluster_ctx.clb_nlist.net_name(net_id).c_str());
                }
            } else if (draw_state->draw_rr_node[tptr->index].color
                       == ezgl::WHITE) {
                // If node is de-selected.
                draw_state->net_color[net_id] = ezgl::BLACK;
                break;
            }
        }
    }
    application.update_message(message);
}

/* If an rr_node has been clicked on, it will be either highlighted in MAGENTA,
 * or de-highlighted in WHITE. If highlighted, and toggle_rr is selected, highlight
 * fan_in into the node in blue and fan_out from the node in red. If de-highlighted,
 * de-highlight its fan_in and fan_out.
 */
void draw_highlight_fan_in_fan_out(const std::set<int>& nodes) {
    t_draw_state* draw_state = get_draw_state_vars();
    auto& device_ctx = g_vpr_ctx.device();
    const auto& rr_graph = device_ctx.rr_graph;

    for (auto node : nodes) {
        /* Highlight the fanout nodes in red. */
        for (t_edge_size iedge = 0, l = rr_graph.num_edges(RRNodeId(node));
             iedge < l; iedge++) {
            int fanout_node = device_ctx.rr_nodes[node].edge_sink_node(iedge);

            if (draw_state->draw_rr_node[node].color == ezgl::MAGENTA
                && draw_state->draw_rr_node[fanout_node].color
                       != ezgl::MAGENTA) {
                // If node is highlighted, highlight its fanout
                draw_state->draw_rr_node[fanout_node].color = DRIVES_IT_COLOR;
                draw_state->draw_rr_node[fanout_node].node_highlighted = true;
            } else if (draw_state->draw_rr_node[node].color == ezgl::WHITE) {
                // If node is de-highlighted, de-highlight its fanout
                draw_state->draw_rr_node[fanout_node].color = DEFAULT_RR_NODE_COLOR;
                draw_state->draw_rr_node[fanout_node].node_highlighted = false;
            }
        }

        /* Highlight the nodes that can fanin to this node in blue. */
        for (size_t inode = 0; inode < device_ctx.rr_nodes.size(); inode++) {
            for (t_edge_size iedge = 0, l = rr_graph.num_edges(RRNodeId(inode)); iedge < l;
                 iedge++) {
                int fanout_node = device_ctx.rr_nodes[inode].edge_sink_node(
                    iedge);
                if (fanout_node == node) {
                    if (draw_state->draw_rr_node[node].color == ezgl::MAGENTA
                        && draw_state->draw_rr_node[inode].color
                               != ezgl::MAGENTA) {
                        // If node is highlighted, highlight its fanin
                        draw_state->draw_rr_node[inode].color = ezgl::BLUE;
                        draw_state->draw_rr_node[inode].node_highlighted = true;
                    } else if (draw_state->draw_rr_node[node].color
                               == ezgl::WHITE) {
                        // If node is de-highlighted, de-highlight its fanin
                        draw_state->draw_rr_node[inode].color = DEFAULT_RR_NODE_COLOR;
                        draw_state->draw_rr_node[inode].node_highlighted = false;
                    }
                }
            }
        }
    }
}

/* This is a helper function for highlight_rr_nodes(). It determines whether
 * a routing resource has been clicked on by computing a bounding box for that
 *  and checking if the mouse click hit inside its bounding box.
 *
 *  It returns the hit RR node's ID (or OPEN if no hit)
 */
static int draw_check_rr_node_hit(float click_x, float click_y) {
    int hit_node = OPEN;
    ezgl::rectangle bound_box;

    t_draw_coords* draw_coords = get_draw_coords_vars();
    auto& device_ctx = g_vpr_ctx.device();
    const auto& rr_graph = device_ctx.rr_graph;

    for (size_t inode = 0; inode < device_ctx.rr_nodes.size(); inode++) {
        RRNodeId rr_node = RRNodeId(inode);
        switch (rr_graph.node_type(rr_node)) {
            case IPIN:
            case OPIN: {
                int i = rr_graph.node_xlow(rr_node);
                int j = rr_graph.node_ylow(rr_node);
                t_physical_tile_type_ptr type = device_ctx.grid[i][j].type;
                int width_offset = device_ctx.grid[i][j].width_offset;
                int height_offset = device_ctx.grid[i][j].height_offset;
                int ipin = rr_graph.node_pin_num(rr_node);
                float xcen, ycen;
                for (const e_side& iside : SIDES) {
                    // If pin exists on this side of the block, then get pin coordinates
                    if (type->pinloc[width_offset][height_offset][size_t(iside)][ipin]) {
                        draw_get_rr_pin_coords(inode, &xcen, &ycen, iside);

                        // Now check if we clicked on this pin
                        if (click_x >= xcen - draw_coords->pin_size && click_x <= xcen + draw_coords->pin_size && click_y >= ycen - draw_coords->pin_size && click_y <= ycen + draw_coords->pin_size) {
                            hit_node = inode;
                            return hit_node;
                        }
                    }
                }
                break;
            }
            case SOURCE:
            case SINK: {
                float xcen, ycen;
                draw_get_rr_src_sink_coords(device_ctx.rr_nodes[inode], &xcen, &ycen);

                // Now check if we clicked on this pin
                if (click_x >= xcen - draw_coords->pin_size && click_x <= xcen + draw_coords->pin_size && click_y >= ycen - draw_coords->pin_size && click_y <= ycen + draw_coords->pin_size) {
                    hit_node = inode;
                    return hit_node;
                }
                break;
            }
            case CHANX:
            case CHANY: {
                bound_box = draw_get_rr_chan_bbox(inode);

                // Check if we clicked on this wire, with 30%
                // tolerance outside its boundary
                const float tolerance = 0.3;
                if (click_x >= bound_box.left() - tolerance && click_x <= bound_box.right() + tolerance && click_y >= bound_box.bottom() - tolerance && click_y <= bound_box.top() + tolerance) {
                    hit_node = inode;
                    return hit_node;
                }
                break;
            }
            default:
                break;
        }
    }
    return hit_node;
}

std::set<int> draw_expand_non_configurable_rr_nodes(int from_node) {
    std::set<int> expanded_nodes;
    draw_expand_non_configurable_rr_nodes_recurr(from_node, expanded_nodes);
    return expanded_nodes;
}

void draw_expand_non_configurable_rr_nodes_recurr(int from_node,
                                                  std::set<int>& expanded_nodes) {
    auto& device_ctx = g_vpr_ctx.device();
    const auto& rr_graph = device_ctx.rr_graph;

    expanded_nodes.insert(from_node);

    for (t_edge_size iedge = 0;
         iedge < rr_graph.num_edges(RRNodeId(from_node)); ++iedge) {
        bool edge_configurable = device_ctx.rr_nodes[from_node].edge_is_configurable(iedge);
        int to_node = device_ctx.rr_nodes[from_node].edge_sink_node(iedge);

        if (!edge_configurable && !expanded_nodes.count(to_node)) {
            draw_expand_non_configurable_rr_nodes_recurr(to_node,
                                                         expanded_nodes);
        }
    }
}

/* This routine is called when the routing resource graph is shown, and someone
 * clicks outside a block. That click might represent a click on a wire -- we call
 * this routine to determine which wire (if any) was clicked on.  If a wire was
 * clicked upon, we highlight it in Magenta, and its fanout in red.
 */
static bool highlight_rr_nodes(float x, float y) {
    t_draw_state* draw_state = get_draw_state_vars();

    if (draw_state->draw_rr_toggle == DRAW_NO_RR && !draw_state->show_nets) {
        application.update_message(draw_state->default_message);
        application.refresh_drawing();
        return false; //No rr shown
    }

    // Check which rr_node (if any) was clicked on.
    int hit_node = draw_check_rr_node_hit(x, y);

    return highlight_rr_nodes(hit_node);
}

#    if defined(X11) && !defined(__MINGW32__)
void act_on_key_press(ezgl::application* /*app*/, GdkEventKey* /*event*/, char* key_name) {
    //VTR_LOG("Key press %c (%d)\n", key_pressed, keysym);
    std::string key(key_name);
}
#    else
void act_on_key_press(ezgl::application* /*app*/, GdkEventKey* /*event*/, char* /*key_name*/) {
}
#    endif

void act_on_mouse_press(ezgl::application* app, GdkEventButton* event, double x, double y) {
    //  std::cout << "User clicked the ";

    if (event->button == 1) {
        //    std::cout << "left ";

        if (window_mode) {
            //click on any two points to form new window rectangle bound

            if (!window_point_1_collected) {
                //collect first point data

                window_point_1_collected = true;
                point_1 = {x, y};
            } else {
                //collect second point data

                //click on any two points to form new window rectangle bound
                ezgl::point2d point_2 = {x, y};
                ezgl::rectangle current_window = (app->get_canvas(
                                                      app->get_main_canvas_id()))
                                                     ->get_camera()
                                                     .get_world();

                //calculate a rectangle with the same ratio based on the two clicks
                double window_ratio = current_window.height()
                                      / current_window.width();
                double new_height = abs(point_1.y - point_2.y);
                double new_width = new_height / window_ratio;

                //zoom in
                ezgl::rectangle new_window = {point_1, {point_1.x + new_width, point_2.y}};
                (app->get_canvas(app->get_main_canvas_id()))->get_camera().set_world(new_window);

                //reset flags
                window_mode = false;
                window_point_1_collected = false;
            }
            app->refresh_drawing();
        } else {
            // regular clicking mode

            /* This routine is called when the user clicks in the graphics area. *
             * It determines if a clb was clicked on.  If one was, it is         *
             * highlighted in green, it's fanin nets and clbs are highlighted in *
             * blue and it's fanout is highlighted in red.  If no clb was        *
             * clicked on (user clicked on white space) any old highlighting is  *
             * removed.  Note that even though global nets are not drawn, their  *
             * fanins and fanouts are highlighted when you click on a block      *
             * attached to them.                                                 */

            /* Control + mouse click to select multiple nets. */
            if (!(event->state & GDK_CONTROL_MASK))
                deselect_all();

            //Check if we hit an rr node
            // Note that we check this before checking for a block, since pins and routing may appear overtop of a multi-width/height block
            if (highlight_rr_nodes(x, y)) {
                return; //Selected an rr node
            }

            highlight_blocks(x, y);
        }
    }
    //  else if (event->button == 2)
    //    std::cout << "middle ";
    //  else if (event->button == 3)
    //    std::cout << "right ";

    //  std::cout << "mouse button at coordinates (" << x << "," << y << ") " << std::endl;
}

void act_on_mouse_move(ezgl::application* app, GdkEventButton* event, double x, double y) {
    //  std::cout << "Mouse move at coordinates (" << x << "," << y << ") "<< std::endl;

    // user has clicked the window button, in window mode
    if (window_point_1_collected) {
        // draw a grey, dashed-line box to indicate the zoom-in region
        app->refresh_drawing();
        ezgl::renderer* g = app->get_renderer();
        g->set_line_dash(ezgl::line_dash::asymmetric_5_3);
        g->set_color(blk_GREY);
        g->set_line_width(2);
        g->draw_rectangle(point_1, {x, y});
        return;
    }

    // user has not clicked the window button, in regular mode
    t_draw_state* draw_state = get_draw_state_vars();

    if (draw_state->draw_rr_toggle != DRAW_NO_RR) {
        int hit_node = draw_check_rr_node_hit(x, y);

        if (hit_node != OPEN) {
            //Update message

            std::string info = describe_rr_node(hit_node);
            std::string msg = vtr::string_fmt("Moused over %s", info.c_str());
            app->update_message(msg.c_str());
        } else {
            if (!rr_highlight_message.empty()) {
                app->update_message(rr_highlight_message.c_str());
            } else {
                app->update_message(draw_state->default_message);
            }
        }
    }
    event = event; // just for hiding warning message
}

void draw_highlight_blocks_color(t_logical_block_type_ptr type,
                                 ClusterBlockId blk_id) {
    int k, iclass;
    ClusterBlockId fanblk;

    t_draw_state* draw_state = get_draw_state_vars();
    auto& cluster_ctx = g_vpr_ctx.clustering();

    for (k = 0; k < type->pb_type->num_pins; k++) { /* Each pin on a CLB */
        ClusterNetId net_id = cluster_ctx.clb_nlist.block_net(blk_id, k);

        if (net_id == ClusterNetId::INVALID())
            continue;

        auto physical_tile = physical_tile_type(blk_id);
        int physical_pin = get_physical_pin(physical_tile, type, k);

        iclass = physical_tile->pin_class[physical_pin];

        if (physical_tile->class_inf[iclass].type == DRIVER) { /* Fanout */
            if (draw_state->block_color(blk_id) == SELECTED_COLOR) {
                /* If block already highlighted, de-highlight the fanout. (the deselect case)*/
                draw_state->net_color[net_id] = ezgl::BLACK;
                for (auto pin_id : cluster_ctx.clb_nlist.net_sinks(net_id)) {
                    fanblk = cluster_ctx.clb_nlist.pin_block(pin_id);
                    draw_reset_blk_color(fanblk);
                }
            } else {
                /* Highlight the fanout */
                draw_state->net_color[net_id] = DRIVES_IT_COLOR;
                for (auto pin_id : cluster_ctx.clb_nlist.net_sinks(net_id)) {
                    fanblk = cluster_ctx.clb_nlist.pin_block(pin_id);
                    draw_state->set_block_color(fanblk, DRIVES_IT_COLOR);
                }
            }
        } else { /* This net is fanin to the block. */
            if (draw_state->block_color(blk_id) == SELECTED_COLOR) {
                /* If block already highlighted, de-highlight the fanin. (the deselect case)*/
                draw_state->net_color[net_id] = ezgl::BLACK;
                fanblk = cluster_ctx.clb_nlist.net_driver_block(net_id); /* DRIVER to net */
                draw_reset_blk_color(fanblk);
            } else {
                /* Highlight the fanin */
                draw_state->net_color[net_id] = DRIVEN_BY_IT_COLOR;
                fanblk = cluster_ctx.clb_nlist.net_driver_block(net_id); /* DRIVER to net */
                draw_state->set_block_color(fanblk, DRIVEN_BY_IT_COLOR);
            }
        }
    }

    if (draw_state->block_color(blk_id) == SELECTED_COLOR) {
        /* If block already highlighted, de-highlight the selected block. */
        draw_reset_blk_color(blk_id);
    } else {
        /* Highlight the selected block. */
        draw_state->set_block_color(blk_id, SELECTED_COLOR);
    }
}

void deselect_all() {
    // Sets the color of all clbs, nets and rr_nodes to the default.
    // as well as clearing the highlighed sub-block

    t_draw_state* draw_state = get_draw_state_vars();
    auto& cluster_ctx = g_vpr_ctx.clustering();
    auto& device_ctx = g_vpr_ctx.device();

    /* Create some colour highlighting */
    for (auto blk_id : cluster_ctx.clb_nlist.blocks()) {
        if (blk_id != ClusterBlockId::INVALID())
            draw_reset_blk_color(blk_id);
    }

    for (auto net_id : cluster_ctx.clb_nlist.nets())
        draw_state->net_color[net_id] = ezgl::BLACK;

    for (size_t i = 0; i < device_ctx.rr_nodes.size(); i++) {
        draw_state->draw_rr_node[i].color = DEFAULT_RR_NODE_COLOR;
        draw_state->draw_rr_node[i].node_highlighted = false;
    }
    get_selected_sub_block_info().clear();
}

static void draw_reset_blk_color(ClusterBlockId blk_id) {
    t_draw_state* draw_state = get_draw_state_vars();
    draw_state->reset_block_color(blk_id);
}

/**
 * Draws a small triangle, at a position along a line from 'start' to 'end'.
 *
 * 'relative_position' [0., 1] defines the triangles position relative to 'start'.
 *
 * A 'relative_position' of 0. draws the triangle centered at 'start'.
 * A 'relative_position' of 1. draws the triangle centered at 'end'.
 * Fractional values draw the triangle along the line
 */
void draw_triangle_along_line(ezgl::renderer* g, ezgl::point2d start, ezgl::point2d end, float relative_position, float arrow_size) {
    VTR_ASSERT(relative_position >= 0. && relative_position <= 1.);
    float xdelta = end.x - start.x;
    float ydelta = end.y - start.y;

    float xtri = start.x + xdelta * relative_position;
    float ytri = start.y + ydelta * relative_position;

    draw_triangle_along_line(g, xtri, ytri, start.x, end.x, start.y, end.y,
                             arrow_size);
}

/* Draws a triangle with it's center at loc, and of length & width
 * arrow_size, rotated such that it points in the direction
 * of the directed line segment start -> end.
 */
void draw_triangle_along_line(ezgl::renderer* g, ezgl::point2d loc, ezgl::point2d start, ezgl::point2d end, float arrow_size) {
    draw_triangle_along_line(g, loc.x, loc.y, start.x, end.x, start.y, end.y,
                             arrow_size);
}

/**
 * Draws a triangle with it's center at (xend, yend), and of length & width
 * arrow_size, rotated such that it points in the direction
 * of the directed line segment (x1, y1) -> (x2, y2).
 *
 * Note that the parameters are in a strange order
 */

void draw_triangle_along_line(ezgl::renderer* g, float xend, float yend, float x1, float x2, float y1, float y2, float arrow_size) {
    float switch_rad = arrow_size / 2;
    float xdelta, ydelta;
    float magnitude;
    float xunit, yunit;
    float xbaseline, ybaseline;

    std::vector<ezgl::point2d> poly;

    xdelta = x2 - x1;
    ydelta = y2 - y1;
    magnitude = sqrt(xdelta * xdelta + ydelta * ydelta);

    xunit = xdelta / magnitude;
    yunit = ydelta / magnitude;

    poly.push_back({xend + xunit * switch_rad, yend + yunit * switch_rad});
    xbaseline = xend - xunit * switch_rad;
    ybaseline = yend - yunit * switch_rad;
    poly.push_back(
        {xbaseline + yunit * switch_rad, ybaseline - xunit * switch_rad});
    poly.push_back(
        {xbaseline - yunit * switch_rad, ybaseline + xunit * switch_rad});

    g->fill_poly(poly);
}

static void draw_pin_to_chan_edge(int pin_node, int chan_node, ezgl::renderer* g) {
    /* This routine draws an edge from the pin_node to the chan_node (CHANX or   *
     * CHANY).  The connection is made to the nearest end of the track instead   *
     * of perpendicular to the track to symbolize a single-drive connection.     */

    /* TODO: Fix this for global routing, currently for detailed only */

    t_draw_coords* draw_coords = get_draw_coords_vars();
    auto& device_ctx = g_vpr_ctx.device();
    const auto& rr_graph = device_ctx.rr_graph;

    //const t_rr_node& pin_rr = device_ctx.rr_nodes[pin_node];
    auto pin_rr = RRNodeId(pin_node);
    auto chan_rr = RRNodeId(chan_node);

    const t_grid_tile& grid_tile = device_ctx.grid[rr_graph.node_xlow(pin_rr)][rr_graph.node_ylow(pin_rr)];
    t_physical_tile_type_ptr grid_type = grid_tile.type;

    float x1 = 0, y1 = 0;
    /* If there is only one side, no need for the following inference!!!
     * When a node may have multiple sides,
     * we lack direct information about which side of the node drives the channel node
     * However, we can infer which side is actually used by the driver based on the
     * coordinates of the channel node.
     * In principle, in a regular rr_graph that can pass check_rr_graph() function,
     * the coordinates should follow the illustration: 
     *
     *                +----------+
     *                |  CHANX   |
     *                |  [x][y]  |
     *                +----------+
     *   +----------+ +----------+ +--------+
     *   |          | |          | |        |
     *   |  CHANY   | |  Grid    | | CHANY  |
     *   | [x-1][y] | | [x][y]   | | [x][y] |
     *   |          | |          | |        |
     *   +----------+ +----------+ +--------+
     *                +----------+
     *                |  CHANX   |
     *                | [x][y-1] |
     *                +----------+
     *
     *
     * Therefore, when there are multiple side:
     * - a TOP side node is considered when the ylow of CHANX >= ylow of the node
     * - a BOTTOM side node is considered when the ylow of CHANX <= ylow - 1 of the node
     * - a RIGHT side node is considered when the xlow of CHANY >= xlow of the node
     * - a LEFT side node is considered when the xlow of CHANY <= xlow - 1 of the node
     *
     * Note: ylow == yhigh for CHANX and xlow == xhigh for CHANY
     *
     * Note: Similar rules are applied for grid that has width > 1 and height > 1
     *       This is because (xlow, ylow) or (xhigh, yhigh) of the node follows 
     *       the actual offset of the pin in the context of grid width and height
     */
    std::vector<e_side> pin_candidate_sides;
    for (const e_side& pin_candidate_side : SIDES) {
        if ((rr_graph.is_node_on_specific_side(pin_rr, pin_candidate_side))
            && (grid_type->pinloc[grid_tile.width_offset][grid_tile.height_offset][pin_candidate_side][rr_graph.node_pin_num(pin_rr)])) {
            pin_candidate_sides.push_back(pin_candidate_side);
        }
    }
    /* Only 1 side will be picked in the end
     * Any rr_node of a grid should have at least 1 side!!!
     */
    e_side pin_side = NUM_SIDES;
    const t_rr_type channel_type = rr_graph.node_type(RRNodeId(chan_node));
    if (1 == pin_candidate_sides.size()) {
        pin_side = pin_candidate_sides[0];
    } else {
        VTR_ASSERT(1 < pin_candidate_sides.size());
        if (CHANX == channel_type && rr_graph.node_ylow(pin_rr) <= rr_graph.node_ylow(chan_rr)) {
            pin_side = TOP;
        } else if (CHANX == channel_type && rr_graph.node_ylow(pin_rr) - 1 >= rr_graph.node_ylow(chan_rr)) {
            pin_side = BOTTOM;
        } else if (CHANY == channel_type && rr_graph.node_xlow(pin_rr) <= rr_graph.node_xlow(chan_rr)) {
            pin_side = RIGHT;
        } else if (CHANY == channel_type && rr_graph.node_xlow(pin_rr) - 1 >= rr_graph.node_xlow(chan_rr)) {
            pin_side = LEFT;
        }
        /* The inferred side must be in the list of sides of the pin rr_node!!! */
        VTR_ASSERT(pin_candidate_sides.end() != std::find(pin_candidate_sides.begin(), pin_candidate_sides.end(), pin_side));
    }
    /* Sanity check */
    VTR_ASSERT(NUM_SIDES != pin_side);

    /* Now we determine which side to be used, calculate the offset for the pin to be drawn
     * - For the pin locates above/right to the grid (at the top/right side), 
     *   a positive offset (+ve) is required
     * - For the pin locates below/left to the grid (at the bottom/left side), 
     *   a negative offset (-ve) is required
     *
     *   y
     *   ^                           +-----+ ---
     *   |                           | PIN |  ^
     *   |                           |     |  offset
     *   |                           |     |  v
     *   |               +-----------+-----+----------+
     *   |               |                            |<- offset ->|
     *   |    |<-offset->|                            +------------+
     *   |    +----------+        Grid                |   PIN      |
     *   |    | PIN      |                            +------------+
     *   |    +----------+                            |
     *   |               |                            |
     *   |               +---+-----+------------------+
     *   |               ^   |     |
     *   |            offset | PIN |
     *   |               v   |     |
     *   |               ----+-----+  
     *   +------------------------------------------------------------>x
     */
    float draw_pin_offset;
    if (TOP == pin_side || RIGHT == pin_side) {
        draw_pin_offset = draw_coords->pin_size;
    } else {
        VTR_ASSERT(BOTTOM == pin_side || LEFT == pin_side);
        draw_pin_offset = -draw_coords->pin_size;
    }

    draw_get_rr_pin_coords(pin_node, &x1, &y1, pin_side);

    ezgl::rectangle chan_bbox = draw_get_rr_chan_bbox(chan_node);

    float x2 = 0, y2 = 0;
    const Direction chan_rr_direction = rr_graph.node_direction(RRNodeId(chan_node));
    switch (channel_type) {
        case CHANX: {
            y1 += draw_pin_offset;
            y2 = chan_bbox.bottom();
            x2 = x1;
            if (is_opin(rr_graph.node_pin_num(pin_rr), grid_type)) {
                if (chan_rr_direction == Direction::INC) {
                    x2 = chan_bbox.left();
                } else if (chan_rr_direction == Direction::DEC) {
                    x2 = chan_bbox.right();
                }
            }
            break;
        }
        case CHANY: {
            x1 += draw_pin_offset;
            x2 = chan_bbox.left();
            y2 = y1;
            if (is_opin(rr_graph.node_pin_num(pin_rr), grid_type)) {
                if (chan_rr_direction == Direction::INC) {
                    y2 = chan_bbox.bottom();
                } else if (chan_rr_direction == Direction::DEC) {
                    y2 = chan_bbox.top();
                }
            }
            break;
        }
        default: {
            vpr_throw(VPR_ERROR_OTHER, __FILE__, __LINE__,
                      "in draw_pin_to_chan_edge: Invalid channel node %d.\n", chan_node);
        }
    }
    g->draw_line({x1, y1}, {x2, y2});

    //don't draw the ex, or triangle unless zoomed in really far
    if (chan_rr_direction == Direction::BIDIR || !is_opin(rr_graph.node_pin_num(pin_rr), grid_type)) {
        draw_x(x2, y2, 0.7 * draw_coords->pin_size, g);
    } else {
        float xend = x2 + (x1 - x2) / 10.;
        float yend = y2 + (y1 - y2) / 10.;
        draw_triangle_along_line(g, xend, yend, x1, x2, y1, y2);
    }
}

static void draw_pin_to_pin(int opin_node, int ipin_node, ezgl::renderer* g) {
    /* This routine draws an edge from the opin rr node to the ipin rr node */
    auto& device_ctx = g_vpr_ctx.device();
    const auto& rr_graph = device_ctx.rr_graph;
    VTR_ASSERT(rr_graph.node_type(RRNodeId(opin_node)) == OPIN);
    VTR_ASSERT(rr_graph.node_type(RRNodeId(ipin_node)) == IPIN);

    /* FIXME: May use a smarter strategy
     * Currently, we use the last side found for both OPIN and IPIN 
     * when draw the direct connection between the two nodes
     * Note: tried first side but see missing connections
     */
    float x1 = 0, y1 = 0;
    std::vector<e_side> opin_candidate_sides;
    for (const e_side& opin_candidate_side : SIDES) {
        if (rr_graph.is_node_on_specific_side(RRNodeId(opin_node), opin_candidate_side)) {
            opin_candidate_sides.push_back(opin_candidate_side);
        }
    }
    VTR_ASSERT(1 <= opin_candidate_sides.size());
    draw_get_rr_pin_coords(opin_node, &x1, &y1, opin_candidate_sides.back());

    float x2 = 0, y2 = 0;
    std::vector<e_side> ipin_candidate_sides;
    for (const e_side& ipin_candidate_side : SIDES) {
        if (rr_graph.is_node_on_specific_side(RRNodeId(ipin_node), ipin_candidate_side)) {
            ipin_candidate_sides.push_back(ipin_candidate_side);
        }
    }
    VTR_ASSERT(1 <= ipin_candidate_sides.size());
    draw_get_rr_pin_coords(ipin_node, &x2, &y2, ipin_candidate_sides.back());

    g->draw_line({x1, y1}, {x2, y2});

    float xend = x2 + (x1 - x2) / 10.;
    float yend = y2 + (y1 - y2) / 10.;
    draw_triangle_along_line(g, xend, yend, x1, x2, y1, y2);
}

static void draw_pin_to_sink(int ipin_node, int sink_node, ezgl::renderer* g) {
    auto& device_ctx = g_vpr_ctx.device();
    const auto& rr_graph = device_ctx.rr_graph;

    float x1 = 0, y1 = 0;
    /* Draw the line for each ipin on different sides */
    for (const e_side& pin_side : SIDES) {
        if (!rr_graph.is_node_on_specific_side(RRNodeId(ipin_node), pin_side)) {
            continue;
        }

        draw_get_rr_pin_coords(ipin_node, &x1, &y1, pin_side);

        float x2 = 0, y2 = 0;
        draw_get_rr_src_sink_coords(device_ctx.rr_nodes[sink_node], &x2, &y2);

        g->draw_line({x1, y1}, {x2, y2});

        float xend = x2 + (x1 - x2) / 10.;
        float yend = y2 + (y1 - y2) / 10.;
        draw_triangle_along_line(g, xend, yend, x1, x2, y1, y2);
    }
}

static void draw_source_to_pin(int source_node, int opin_node, ezgl::renderer* g) {
    auto& device_ctx = g_vpr_ctx.device();
    const auto& rr_graph = device_ctx.rr_graph;

    float x1 = 0, y1 = 0;
    draw_get_rr_src_sink_coords(device_ctx.rr_nodes[source_node], &x1, &y1);

    /* Draw the line for each ipin on different sides */
    for (const e_side& pin_side : SIDES) {
        if (!rr_graph.is_node_on_specific_side(RRNodeId(opin_node), pin_side)) {
            continue;
        }

        float x2 = 0, y2 = 0;
        draw_get_rr_pin_coords(opin_node, &x2, &y2, pin_side);

        g->draw_line({x1, y1}, {x2, y2});

        float xend = x2 + (x1 - x2) / 10.;
        float yend = y2 + (y1 - y2) / 10.;
        draw_triangle_along_line(g, xend, yend, x1, x2, y1, y2);
    }
}

static inline void draw_mux_with_size(ezgl::point2d origin, e_side orientation, float height, int size, ezgl::renderer* g) {
    g->set_color(ezgl::YELLOW);
    auto bounds = draw_mux(origin, orientation, height, g);

    g->set_color(ezgl::BLACK);
    g->draw_text(bounds.center(), std::to_string(size), bounds.width(),
                 bounds.height());
}

//Draws a mux
static inline ezgl::rectangle draw_mux(ezgl::point2d origin, e_side orientation, float height, ezgl::renderer* g) {
    return draw_mux(origin, orientation, height, 0.4 * height, 0.6, g);
}

//Draws a mux, height/width define the bounding box, scale [0.,1.] controls the slope of the muxes sides
static inline ezgl::rectangle draw_mux(ezgl::point2d origin, e_side orientation, float height, float width, float scale, ezgl::renderer* g) {
    std::vector<ezgl::point2d> mux_polygon;

    switch (orientation) {
        case TOP:
            //Clock-wise from bottom left
            mux_polygon.push_back({origin.x - height / 2, origin.y - width / 2});
            mux_polygon.push_back(
                {origin.x - (scale * height) / 2, origin.y + width / 2});
            mux_polygon.push_back(
                {origin.x + (scale * height) / 2, origin.y + width / 2});
            mux_polygon.push_back({origin.x + height / 2, origin.y - width / 2});
            break;
        case BOTTOM:
            //Clock-wise from bottom left
            mux_polygon.push_back(
                {origin.x - (scale * height) / 2, origin.y - width / 2});
            mux_polygon.push_back({origin.x - height / 2, origin.y + width / 2});
            mux_polygon.push_back({origin.x + height / 2, origin.y + width / 2});
            mux_polygon.push_back(
                {origin.x + (scale * height) / 2, origin.y - width / 2});
            break;
        case LEFT:
            //Clock-wise from bottom left
            mux_polygon.push_back(
                {origin.x - width / 2, origin.y - (scale * height) / 2});
            mux_polygon.push_back(
                {origin.x - width / 2, origin.y + (scale * height) / 2});
            mux_polygon.push_back({origin.x + width / 2, origin.y + height / 2});
            mux_polygon.push_back({origin.x + width / 2, origin.y - height / 2});
            break;
        case RIGHT:
            //Clock-wise from bottom left
            mux_polygon.push_back({origin.x - width / 2, origin.y - height / 2});
            mux_polygon.push_back({origin.x - width / 2, origin.y + height / 2});
            mux_polygon.push_back(
                {origin.x + width / 2, origin.y + (scale * height) / 2});
            mux_polygon.push_back(
                {origin.x + width / 2, origin.y - (scale * height) / 2});
            break;

        default:
            VTR_ASSERT_MSG(false, "Unrecognized orientation");
    }
    g->fill_poly(mux_polygon);

    ezgl::point2d min((float)mux_polygon[0].x, (float)mux_polygon[0].y);
    ezgl::point2d max((float)mux_polygon[0].x, (float)mux_polygon[0].y);
    for (const auto& point : mux_polygon) {
        min.x = std::min((float)min.x, (float)point.x);
        min.y = std::min((float)min.y, (float)point.y);
        max.x = std::max((float)max.x, (float)point.x);
        max.y = std::max((float)max.y, (float)point.y);
    }

    return ezgl::rectangle(min, max);
}

ezgl::point2d tnode_draw_coord(tatum::NodeId node) {
    auto& atom_ctx = g_vpr_ctx.atom();

    AtomPinId pin = atom_ctx.lookup.tnode_atom_pin(node);
    return atom_pin_draw_coord(pin);
}

ezgl::point2d atom_pin_draw_coord(AtomPinId pin) {
    auto& atom_ctx = g_vpr_ctx.atom();

    AtomBlockId blk = atom_ctx.nlist.pin_block(pin);
    ClusterBlockId clb_index = atom_ctx.lookup.atom_clb(blk);
    const t_pb_graph_node* pg_gnode = atom_ctx.lookup.atom_pb_graph_node(blk);

    t_draw_coords* draw_coords = get_draw_coords_vars();
    ezgl::rectangle pb_bbox = draw_coords->get_absolute_pb_bbox(clb_index,
                                                                pg_gnode);

    //We place each atom pin inside it's pb bounding box
    //and distribute the pins along it's vertical centre line
    const float FRACTION_USABLE_WIDTH = 0.8;
    float width = pb_bbox.width();
    float usable_width = width * FRACTION_USABLE_WIDTH;
    float x_offset = pb_bbox.left() + width * (1 - FRACTION_USABLE_WIDTH) / 2;

    int pin_index, pin_total;
    find_pin_index_at_model_scope(pin, blk, &pin_index, &pin_total);

    const ezgl::point2d point = {x_offset + usable_width * pin_index / ((float)pin_total),
                                 pb_bbox.center_y()};

    return point;
}

static void draw_crit_path(ezgl::renderer* g) {
    tatum::TimingPathCollector path_collector;

    t_draw_state* draw_state = get_draw_state_vars();
    auto& timing_ctx = g_vpr_ctx.timing();

    if (draw_state->show_crit_path == DRAW_NO_CRIT_PATH) {
        return;
    }

    if (!draw_state->setup_timing_info) {
        return; //No timing to draw
    }

    //Get the worst timing path
    auto paths = path_collector.collect_worst_setup_timing_paths(
        *timing_ctx.graph,
        *(draw_state->setup_timing_info->setup_analyzer()), 1);
    tatum::TimingPath path = paths[0];

    //Walk through the timing path drawing each edge
    tatum::NodeId prev_node;
    float prev_arr_time = std::numeric_limits<float>::quiet_NaN();
    int i = 0;
    for (tatum::TimingPathElem elem : path.data_arrival_path().elements()) {
        tatum::NodeId node = elem.node();
        float arr_time = elem.tag().time();
        if (prev_node) {
            //We draw each 'edge' in a different color, this allows users to identify the stages and
            //any routing which corresponds to the edge
            //
            //We pick colors from the kelly max-contrast list, for long paths there may be repeats
            ezgl::color color = kelly_max_contrast_colors[i++
                                                          % kelly_max_contrast_colors.size()];

            float delay = arr_time - prev_arr_time;
            if (draw_state->show_crit_path == DRAW_CRIT_PATH_FLYLINES
                || draw_state->show_crit_path
                       == DRAW_CRIT_PATH_FLYLINES_DELAYS) {
                g->set_color(color);
                g->set_line_dash(ezgl::line_dash::none);
                g->set_line_width(4);
                draw_flyline_timing_edge(tnode_draw_coord(prev_node),
                                         tnode_draw_coord(node), delay, g);
            } else {
                VTR_ASSERT(draw_state->show_crit_path != DRAW_NO_CRIT_PATH);

                //Draw the routed version of the timing edge
                draw_routed_timing_edge(prev_node, node, delay, color, g);
            }
        }
        prev_node = node;
        prev_arr_time = arr_time;
    }
}

static void draw_flyline_timing_edge(ezgl::point2d start, ezgl::point2d end, float incr_delay, ezgl::renderer* g) {
    g->draw_line(start, end);
    draw_triangle_along_line(g, start, end, 0.95, 40 * DEFAULT_ARROW_SIZE);
    draw_triangle_along_line(g, start, end, 0.05, 40 * DEFAULT_ARROW_SIZE);

    bool draw_delays = (get_draw_state_vars()->show_crit_path
                            == DRAW_CRIT_PATH_FLYLINES_DELAYS
                        || get_draw_state_vars()->show_crit_path
                               == DRAW_CRIT_PATH_ROUTING_DELAYS);
    if (draw_delays) {
        //Determine the strict bounding box based on the lines start/end
        float min_x = std::min(start.x, end.x);
        float max_x = std::max(start.x, end.x);
        float min_y = std::min(start.y, end.y);
        float max_y = std::max(start.y, end.y);

        //If we have a nearly horizontal/vertical line the bbox is too
        //small to draw the text, so widen it by a tile (i.e. CLB) width
        float tile_width = get_draw_coords_vars()->get_tile_width();
        if (max_x - min_x < tile_width) {
            max_x += tile_width / 2;
            min_x -= tile_width / 2;
        }
        if (max_y - min_y < tile_width) {
            max_y += tile_width / 2;
            min_y -= tile_width / 2;
        }

        //TODO: draw the delays nicer
        //   * rotate to match edge
        //   * offset from line
        //   * track visible in window
        ezgl::rectangle text_bbox({min_x, min_y}, {max_x, max_y});

        std::stringstream ss;
        ss.precision(3);
        ss << 1e9 * incr_delay; //In nanoseconds
        std::string incr_delay_str = ss.str();

        // Get the angle of line, to rotate the text
        float text_angle = (180 / M_PI)
                           * atan((end.y - start.y) / (end.x - start.x));

        // Get the screen coordinates for text drawing
        ezgl::rectangle screen_coords = g->world_to_screen(text_bbox);
        g->set_text_rotation(text_angle);

        // Set the text colour to black to differentiate it from the line
        g->set_font_size(16);
        g->set_color(ezgl::color(0, 0, 0));

        g->set_coordinate_system(ezgl::SCREEN);

        // Find an offset so it is sitting on top/below of the line
        float x_offset = screen_coords.center().x
                         - 8 * sin(text_angle * (M_PI / 180));
        float y_offset = screen_coords.center().y
                         - 8 * cos(text_angle * (M_PI / 180));

        ezgl::point2d offset_text_bbox(x_offset, y_offset);
        g->draw_text(offset_text_bbox, incr_delay_str.c_str(),
                     text_bbox.width(), text_bbox.height());

        g->set_font_size(14);

        g->set_text_rotation(0);
        g->set_coordinate_system(ezgl::WORLD);
    }
}

static void draw_routed_timing_edge(tatum::NodeId start_tnode,
                                    tatum::NodeId end_tnode,
                                    float incr_delay,
                                    ezgl::color color,
                                    ezgl::renderer* g) {
    draw_routed_timing_edge_connection(start_tnode, end_tnode, color, g);

    g->set_line_dash(ezgl::line_dash::asymmetric_5_3);
    g->set_line_width(3);
    g->set_color(color);

    draw_flyline_timing_edge((ezgl::point2d)tnode_draw_coord(start_tnode),
                             (ezgl::point2d)tnode_draw_coord(end_tnode), (float)incr_delay,
                             (ezgl::renderer*)g);

    g->set_line_width(0);
    g->set_line_dash(ezgl::line_dash::none);
}

//Collect all the drawing locations associated with the timing edge between start and end
static void draw_routed_timing_edge_connection(tatum::NodeId src_tnode,
                                               tatum::NodeId sink_tnode,
                                               ezgl::color color,
                                               ezgl::renderer* g) {
    auto& atom_ctx = g_vpr_ctx.atom();
    auto& cluster_ctx = g_vpr_ctx.clustering();
    auto& timing_ctx = g_vpr_ctx.timing();

    AtomPinId atom_src_pin = atom_ctx.lookup.tnode_atom_pin(src_tnode);
    AtomPinId atom_sink_pin = atom_ctx.lookup.tnode_atom_pin(sink_tnode);

    std::vector<ezgl::point2d> points;
    points.push_back(atom_pin_draw_coord(atom_src_pin));

    tatum::EdgeId tedge = timing_ctx.graph->find_edge(src_tnode, sink_tnode);
    tatum::EdgeType edge_type = timing_ctx.graph->edge_type(tedge);

    ClusterNetId net_id = ClusterNetId::INVALID();

    //We currently only trace interconnect edges in detail, and treat all others
    //as flylines
    if (edge_type == tatum::EdgeType::INTERCONNECT) {
        //All atom pins are implemented inside CLBs, so next hop is to the top-level CLB pins

        //TODO: most of this code is highly similar to code in PostClusterDelayCalculator, refactor
        //      into a common method for walking the clustered netlist, this would also (potentially)
        //      allow us to grab the component delays
        AtomBlockId atom_src_block = atom_ctx.nlist.pin_block(atom_src_pin);
        AtomBlockId atom_sink_block = atom_ctx.nlist.pin_block(atom_sink_pin);

        ClusterBlockId clb_src_block = atom_ctx.lookup.atom_clb(atom_src_block);
        VTR_ASSERT(clb_src_block != ClusterBlockId::INVALID());
        ClusterBlockId clb_sink_block = atom_ctx.lookup.atom_clb(
            atom_sink_block);
        VTR_ASSERT(clb_sink_block != ClusterBlockId::INVALID());

        const t_pb_graph_pin* sink_gpin = atom_ctx.lookup.atom_pin_pb_graph_pin(
            atom_sink_pin);
        VTR_ASSERT(sink_gpin);

        int sink_pb_route_id = sink_gpin->pin_count_in_cluster;

        int sink_block_pin_index = -1;
        int sink_net_pin_index = -1;

        std::tie(net_id, sink_block_pin_index, sink_net_pin_index) = find_pb_route_clb_input_net_pin(clb_sink_block,
                                                                                                     sink_pb_route_id);
        if (net_id != ClusterNetId::INVALID() && sink_block_pin_index != -1
            && sink_net_pin_index != -1) {
            //Connection leaves the CLB
            //Now that we have the CLB source and sink pins, we need to grab all the points on the routing connecting the pins
            VTR_ASSERT(
                cluster_ctx.clb_nlist.net_driver_block(net_id)
                == clb_src_block);

            std::vector<int> routed_rr_nodes = trace_routed_connection_rr_nodes(
                net_id, 0, sink_net_pin_index);

            //Mark all the nodes highlighted
            t_draw_state* draw_state = get_draw_state_vars();
            for (int inode : routed_rr_nodes) {
                draw_state->draw_rr_node[inode].color = color;
            }

            draw_partial_route((std::vector<int>)routed_rr_nodes,
                               (ezgl::renderer*)g);
        } else {
            //Connection entirely within the CLB, we don't draw the internal routing so treat it as a fly-line
            VTR_ASSERT(clb_src_block == clb_sink_block);
        }
    }

    points.push_back(atom_pin_draw_coord(atom_sink_pin));
}

//Returns the set of rr nodes which connect driver to sink
static std::vector<int> trace_routed_connection_rr_nodes(
    const ClusterNetId net_id,
    const int driver_pin,
    const int sink_pin) {
    auto& route_ctx = g_vpr_ctx.routing();

    bool allocated_route_tree_structs = alloc_route_tree_timing_structs(true); //Needed for traceback_to_route_tree

    //Conver the traceback into an easily search-able
    t_rt_node* rt_root = traceback_to_route_tree(net_id);

    VTR_ASSERT(
        rt_root
        && rt_root->inode
               == route_ctx.net_rr_terminals[net_id][driver_pin]);

    int sink_rr_node = route_ctx.net_rr_terminals[net_id][sink_pin];

    std::vector<int> rr_nodes_on_path;

    //Collect the rr nodes
    trace_routed_connection_rr_nodes_recurr(rt_root, sink_rr_node,
                                            rr_nodes_on_path);

    //Traced from sink to source, but we want to draw from source to sink
    std::reverse(rr_nodes_on_path.begin(), rr_nodes_on_path.end());

    free_route_tree(rt_root);

    if (allocated_route_tree_structs) {
        free_route_tree_timing_structs();
    }
    return rr_nodes_on_path;
}

//Helper function for trace_routed_connection_rr_nodes
//Adds the rr nodes linking rt_node to sink_rr_node to rr_nodes_on_path
//Returns true if rt_node is on the path
bool trace_routed_connection_rr_nodes_recurr(const t_rt_node* rt_node,
                                             int sink_rr_node,
                                             std::vector<int>& rr_nodes_on_path) {
    //DFS from the current rt_node to the sink_rr_node, when the sink is found trace back the used rr nodes

    if (rt_node->inode == sink_rr_node) {
        rr_nodes_on_path.push_back(sink_rr_node);
        return true;
    }

    for (t_linked_rt_edge* edge = rt_node->u.child_list; edge != nullptr; edge = edge->next) {
        t_rt_node* child_rt_node = edge->child;
        VTR_ASSERT(child_rt_node);

        bool on_path_to_sink = trace_routed_connection_rr_nodes_recurr(
            child_rt_node, sink_rr_node, rr_nodes_on_path);

        if (on_path_to_sink) {
            rr_nodes_on_path.push_back(rt_node->inode);
            return true;
        }
    }

    return false; //Not on path to sink
}

//Find the edge between two rr nodes
static t_edge_size find_edge(int prev_inode, int inode) {
    auto& device_ctx = g_vpr_ctx.device();
    const auto& rr_graph = device_ctx.rr_graph;
    for (t_edge_size iedge = 0;
         iedge < rr_graph.num_edges(RRNodeId(prev_inode)); ++iedge) {
        if (device_ctx.rr_nodes[prev_inode].edge_sink_node(iedge) == inode) {
            return iedge;
        }
    }
    VTR_ASSERT(false);
    return OPEN;
}

ezgl::color to_ezgl_color(vtr::Color<float> color) {
    return ezgl::color(color.r * 255, color.g * 255, color.b * 255);
}

static void draw_color_map_legend(const vtr::ColorMap& cmap,
                                  ezgl::renderer* g) {
    constexpr float LEGEND_WIDTH_FAC = 0.075;
    constexpr float LEGEND_VERT_OFFSET_FAC = 0.05;
    constexpr float TEXT_OFFSET = 10;
    constexpr size_t NUM_COLOR_POINTS = 1000;

    g->set_coordinate_system(ezgl::SCREEN);

    float screen_width = application.get_canvas(
                                        application.get_main_canvas_id())
                             ->width();
    float screen_height = application.get_canvas(
                                         application.get_main_canvas_id())
                              ->height();
    float vert_offset = screen_height * LEGEND_VERT_OFFSET_FAC;
    float legend_width = std::min<int>(LEGEND_WIDTH_FAC * screen_width, 100);

    // In SCREEN coordinate: bottom_left is (0,0), right_top is (screen_width, screen_height)
    ezgl::rectangle legend({0, vert_offset},
                           {legend_width, screen_height - vert_offset});

    float range = cmap.max() - cmap.min();
    float height_incr = legend.height() / float(NUM_COLOR_POINTS);
    for (size_t i = 0; i < NUM_COLOR_POINTS; ++i) {
        float val = cmap.min() + (float(i) / NUM_COLOR_POINTS) * range;
        ezgl::color color = to_ezgl_color(cmap.color(val));

        g->set_color(color);
        g->fill_rectangle({legend.left(), legend.top() - i * height_incr}, {legend.right(), legend.top() - (i + 1) * height_incr});
    }

    //Min mark
    g->set_color(blk_SKYBLUE); // set to skyblue so its easier to see
    std::string str = vtr::string_fmt("%.3g", cmap.min());
    g->draw_text({legend.center_x(), legend.top() - TEXT_OFFSET},
                 str.c_str());

    //Mid marker
    g->set_color(ezgl::BLACK);
    str = vtr::string_fmt("%.3g", cmap.min() + (cmap.range() / 2.));
    g->draw_text({legend.center_x(), legend.center_y()}, str.c_str());

    //Max marker
    g->set_color(ezgl::BLACK);
    str = vtr::string_fmt("%.3g", cmap.max());
    g->draw_text({legend.center_x(), legend.bottom() + TEXT_OFFSET},
                 str.c_str());

    g->set_color(ezgl::BLACK);
    g->draw_rectangle(legend);

    g->set_coordinate_system(ezgl::WORLD);
}

ezgl::color get_block_type_color(t_physical_tile_type_ptr type) {
    //Wrap around if there are too many blocks
    // This ensures we support an arbitrary number of types,
    // although the colours may repeat
    ezgl::color color = block_colors[type->index % block_colors.size()];

    return color;
}

//Lightens a color's luminance [0, 1] by an aboslute 'amount'
ezgl::color lighten_color(ezgl::color color, float amount) {
    constexpr double MAX_LUMINANCE = 0.95; //Clip luminance so it doesn't go full white
    auto hsl = color2hsl(color);

    hsl.l = std::max(0., std::min(MAX_LUMINANCE, hsl.l + amount));

    return hsl2color(hsl);
}

static void draw_block_pin_util() {
    t_draw_state* draw_state = get_draw_state_vars();
    if (draw_state->show_blk_pin_util == DRAW_NO_BLOCK_PIN_UTIL)
        return;

    auto& device_ctx = g_vpr_ctx.device();
    auto& cluster_ctx = g_vpr_ctx.clustering();

    std::map<t_physical_tile_type_ptr, size_t> total_input_pins;
    std::map<t_physical_tile_type_ptr, size_t> total_output_pins;
    for (const auto& type : device_ctx.physical_tile_types) {
        if (is_empty_type(&type)) {
            continue;
        }

        total_input_pins[&type] = type.num_input_pins + type.num_clock_pins;
        total_output_pins[&type] = type.num_output_pins;
    }

    auto blks = cluster_ctx.clb_nlist.blocks();
    vtr::vector<ClusterBlockId, float> pin_util(blks.size());
    for (auto blk : blks) {
        auto type = physical_tile_type(blk);

        if (draw_state->show_blk_pin_util == DRAW_BLOCK_PIN_UTIL_TOTAL) {
            pin_util[blk] = cluster_ctx.clb_nlist.block_pins(blk).size()
                            / float(total_input_pins[type] + total_output_pins[type]);
        } else if (draw_state->show_blk_pin_util
                   == DRAW_BLOCK_PIN_UTIL_INPUTS) {
            pin_util[blk] = (cluster_ctx.clb_nlist.block_input_pins(blk).size()
                             + cluster_ctx.clb_nlist.block_clock_pins(blk).size())
                            / float(total_input_pins[type]);
        } else if (draw_state->show_blk_pin_util
                   == DRAW_BLOCK_PIN_UTIL_OUTPUTS) {
            pin_util[blk] = (cluster_ctx.clb_nlist.block_output_pins(blk).size())
                            / float(total_output_pins[type]);
        } else {
            VTR_ASSERT(false);
        }
    }

    std::unique_ptr<vtr::ColorMap> cmap = std::make_unique<vtr::PlasmaColorMap>(
        0., 1.);

    for (auto blk : blks) {
        ezgl::color color = to_ezgl_color(cmap->color(pin_util[blk]));
        draw_state->set_block_color(blk, color);
    }

    draw_state->color_map = std::move(cmap);

    if (draw_state->show_blk_pin_util == DRAW_BLOCK_PIN_UTIL_TOTAL) {
        application.update_message("Block Total Pin Utilization");
    } else if (draw_state->show_blk_pin_util == DRAW_BLOCK_PIN_UTIL_INPUTS) {
        application.update_message("Block Input Pin Utilization");

    } else if (draw_state->show_blk_pin_util == DRAW_BLOCK_PIN_UTIL_OUTPUTS) {
        application.update_message("Block Output Pin Utilization");
    } else {
        VTR_ASSERT(false);
    }
}

static void draw_reset_blk_colors() {
    auto& cluster_ctx = g_vpr_ctx.clustering();
    auto blks = cluster_ctx.clb_nlist.blocks();
    for (auto blk : blks) {
        draw_reset_blk_color(blk);
    }
}

static void draw_routing_util(ezgl::renderer* g) {
    t_draw_state* draw_state = get_draw_state_vars();
    if (draw_state->show_routing_util == DRAW_NO_ROUTING_UTIL) {
        return;
    }

    t_draw_coords* draw_coords = get_draw_coords_vars();
    auto& device_ctx = g_vpr_ctx.device();

    auto chanx_usage = calculate_routing_usage(CHANX);
    auto chany_usage = calculate_routing_usage(CHANY);

    auto chanx_avail = calculate_routing_avail(CHANX);
    auto chany_avail = calculate_routing_avail(CHANY);

    float min_util = 0.;
    float max_util = -std::numeric_limits<float>::infinity();
    for (size_t x = 0; x < device_ctx.grid.width() - 1; ++x) {
        for (size_t y = 0; y < device_ctx.grid.height() - 1; ++y) {
            max_util = std::max(max_util,
                                routing_util(chanx_usage[x][y], chanx_avail[x][y]));
            max_util = std::max(max_util,
                                routing_util(chany_usage[x][y], chany_avail[x][y]));
        }
    }
    max_util = std::max(max_util, 1.f);

    std::unique_ptr<vtr::ColorMap> cmap;

    if (draw_state->clip_routing_util) {
        cmap = std::make_unique<vtr::PlasmaColorMap>(0., 1.);
    } else {
        cmap = std::make_unique<vtr::PlasmaColorMap>(min_util, max_util);
    }

    float tile_width = draw_coords->get_tile_width();
    float tile_height = draw_coords->get_tile_height();

    float ALPHA = 0.95;
    if (draw_state->show_routing_util == DRAW_ROUTING_UTIL_OVER_BLOCKS) {
        ALPHA = 1.;
    }

    for (size_t x = 0; x < device_ctx.grid.width() - 1; ++x) {
        for (size_t y = 0; y < device_ctx.grid.height() - 1; ++y) {
            float sb_util = 0;
            float chanx_util = 0;
            float chany_util = 0;
            int chan_count = 0;
            if (x > 0) {
                chanx_util = routing_util(chanx_usage[x][y], chanx_avail[x][y]);
                if (draw_state->clip_routing_util) {
                    chanx_util = std::min(chanx_util, 1.f);
                }
                ezgl::color chanx_color = to_ezgl_color(
                    cmap->color(chanx_util));
                chanx_color.alpha *= ALPHA;
                g->set_color(chanx_color);
                ezgl::rectangle bb(
                    {draw_coords->tile_x[x], draw_coords->tile_y[y]
                                                 + 1 * tile_height},
                    {draw_coords->tile_x[x] + 1 * tile_width,
                     draw_coords->tile_y[y + 1]});
                g->fill_rectangle(bb);

                g->set_color(ezgl::BLACK);
                if (draw_state->show_routing_util
                    == DRAW_ROUTING_UTIL_WITH_VALUE) {
                    g->draw_text(bb.center(),
                                 vtr::string_fmt("%.2f", chanx_util).c_str(),
                                 bb.width(), bb.height());
                } else if (draw_state->show_routing_util
                           == DRAW_ROUTING_UTIL_WITH_FORMULA) {
                    g->draw_text(bb.center(),
                                 vtr::string_fmt("%.2f = %.0f / %.0f", chanx_util,
                                                 chanx_usage[x][y], chanx_avail[x][y])
                                     .c_str(),
                                 bb.width(), bb.height());
                }

                sb_util += chanx_util;
                ++chan_count;
            }

            if (y > 0) {
                chany_util = routing_util(chany_usage[x][y], chany_avail[x][y]);
                if (draw_state->clip_routing_util) {
                    chany_util = std::min(chany_util, 1.f);
                }
                ezgl::color chany_color = to_ezgl_color(
                    cmap->color(chany_util));
                chany_color.alpha *= ALPHA;
                g->set_color(chany_color);
                ezgl::rectangle bb({draw_coords->tile_x[x] + 1 * tile_width,
                                    draw_coords->tile_y[y]},
                                   {draw_coords->tile_x[x + 1], draw_coords->tile_y[y]
                                                                    + 1 * tile_height});
                g->fill_rectangle(bb);

                g->set_color(ezgl::BLACK);
                if (draw_state->show_routing_util
                    == DRAW_ROUTING_UTIL_WITH_VALUE) {
                    g->draw_text(bb.center(),
                                 vtr::string_fmt("%.2f", chany_util).c_str(),
                                 bb.width(), bb.height());
                } else if (draw_state->show_routing_util
                           == DRAW_ROUTING_UTIL_WITH_FORMULA) {
                    g->draw_text(bb.center(),
                                 vtr::string_fmt("%.2f = %.0f / %.0f", chany_util,
                                                 chany_usage[x][y], chany_avail[x][y])
                                     .c_str(),
                                 bb.width(), bb.height());
                }

                sb_util += chany_util;
                ++chan_count;
            }

            //For now SB util is just average of surrounding channels
            //TODO: calculate actual usage
            sb_util += routing_util(chanx_usage[x + 1][y],
                                    chanx_avail[x + 1][y]);
            chan_count += 1;
            sb_util += routing_util(chany_usage[x][y + 1],
                                    chany_avail[x][y + 1]);
            chan_count += 1;

            VTR_ASSERT(chan_count > 0);
            sb_util /= chan_count;
            if (draw_state->clip_routing_util) {
                sb_util = std::min(sb_util, 1.f);
            }
            ezgl::color sb_color = to_ezgl_color(cmap->color(sb_util));
            sb_color.alpha *= ALPHA;
            g->set_color(sb_color);
            ezgl::rectangle bb(
                {draw_coords->tile_x[x] + 1 * tile_width,
                 draw_coords->tile_y[y] + 1 * tile_height},
                {draw_coords->tile_x[x + 1], draw_coords->tile_y[y + 1]});
            g->fill_rectangle(bb);

            //Draw over blocks
            if (draw_state->show_routing_util
                == DRAW_ROUTING_UTIL_OVER_BLOCKS) {
                if (x < device_ctx.grid.width() - 2
                    && y < device_ctx.grid.height() - 2) {
                    ezgl::rectangle bb2({draw_coords->tile_x[x + 1],
                                         draw_coords->tile_y[y + 1]},
                                        {draw_coords->tile_x[x + 1] + 1 * tile_width,
                                         draw_coords->tile_y[y + 1] + 1 * tile_width});
                    g->fill_rectangle(bb2);
                }
            }
            g->set_color(ezgl::BLACK);
            if (draw_state->show_routing_util == DRAW_ROUTING_UTIL_WITH_VALUE
                || draw_state->show_routing_util
                       == DRAW_ROUTING_UTIL_WITH_FORMULA) {
                g->draw_text(bb.center(),
                             vtr::string_fmt("%.2f", sb_util).c_str(), bb.width(),
                             bb.height());
            }
        }
    }

    draw_state->color_map = std::move(cmap);
}

static float get_router_expansion_cost(const t_rr_node_route_inf node_inf,
                                       e_draw_router_expansion_cost draw_router_expansion_cost) {
    if (draw_router_expansion_cost == DRAW_ROUTER_EXPANSION_COST_TOTAL
        || draw_router_expansion_cost
               == DRAW_ROUTER_EXPANSION_COST_TOTAL_WITH_EDGES) {
        return node_inf.path_cost;
    } else if (draw_router_expansion_cost == DRAW_ROUTER_EXPANSION_COST_KNOWN
               || draw_router_expansion_cost
                      == DRAW_ROUTER_EXPANSION_COST_KNOWN_WITH_EDGES) {
        return node_inf.backward_path_cost;
    } else if (draw_router_expansion_cost == DRAW_ROUTER_EXPANSION_COST_EXPECTED
               || draw_router_expansion_cost
                      == DRAW_ROUTER_EXPANSION_COST_EXPECTED_WITH_EDGES) {
        return node_inf.path_cost - node_inf.backward_path_cost;
    }

    VPR_THROW(VPR_ERROR_DRAW, "Invalid Router RR cost drawing type");
}

static void draw_router_expansion_costs(ezgl::renderer* g) {
    t_draw_state* draw_state = get_draw_state_vars();
    if (draw_state->show_router_expansion_cost
        == DRAW_NO_ROUTER_EXPANSION_COST) {
        return;
    }

    auto& device_ctx = g_vpr_ctx.device();
    auto& routing_ctx = g_vpr_ctx.routing();

    std::vector<float> rr_costs(device_ctx.rr_nodes.size());

    for (size_t inode = 0; inode < device_ctx.rr_nodes.size(); ++inode) {
        float cost = get_router_expansion_cost(
            routing_ctx.rr_node_route_inf[inode],
            draw_state->show_router_expansion_cost);
        rr_costs[inode] = cost;
    }

    bool all_nan = true;
    for (size_t inode = 0; inode < device_ctx.rr_nodes.size(); ++inode) {
        if (std::isinf(rr_costs[inode])) {
            rr_costs[inode] = NAN;
        } else {
            all_nan = false;
        }
    }

    if (!all_nan) {
        draw_rr_costs(g, rr_costs, false);
    }
    if (draw_state->show_router_expansion_cost
            == DRAW_ROUTER_EXPANSION_COST_TOTAL
        || draw_state->show_router_expansion_cost
               == DRAW_ROUTER_EXPANSION_COST_TOTAL_WITH_EDGES) {
        application.update_message(
            "Routing Expected Total Cost (known + estimate)");
    } else if (draw_state->show_router_expansion_cost
                   == DRAW_ROUTER_EXPANSION_COST_KNOWN
               || draw_state->show_router_expansion_cost
                      == DRAW_ROUTER_EXPANSION_COST_KNOWN_WITH_EDGES) {
        application.update_message("Routing Known Cost (from source to node)");
    } else if (draw_state->show_router_expansion_cost
                   == DRAW_ROUTER_EXPANSION_COST_EXPECTED
               || draw_state->show_router_expansion_cost
                      == DRAW_ROUTER_EXPANSION_COST_EXPECTED_WITH_EDGES) {
        application.update_message(
            "Routing Expected Cost (from node to target)");
    } else {
        VPR_THROW(VPR_ERROR_DRAW, "Invalid Router RR cost drawing type");
    }
}

static void draw_rr_costs(ezgl::renderer* g, const std::vector<float>& rr_costs, bool lowest_cost_first) {
    t_draw_state* draw_state = get_draw_state_vars();

    /* Draws routing costs */

    auto& device_ctx = g_vpr_ctx.device();
    const auto& rr_graph = device_ctx.rr_graph;

    g->set_line_width(0);

    bool with_edges = (draw_state->show_router_expansion_cost == DRAW_ROUTER_EXPANSION_COST_TOTAL_WITH_EDGES
                       || draw_state->show_router_expansion_cost == DRAW_ROUTER_EXPANSION_COST_KNOWN_WITH_EDGES
                       || draw_state->show_router_expansion_cost == DRAW_ROUTER_EXPANSION_COST_EXPECTED_WITH_EDGES);

    VTR_ASSERT(rr_costs.size() == device_ctx.rr_nodes.size());

    float min_cost = std::numeric_limits<float>::infinity();
    float max_cost = -min_cost;
    for (size_t inode = 0; inode < device_ctx.rr_nodes.size(); inode++) {
        if (std::isnan(rr_costs[inode])) continue;

        min_cost = std::min(min_cost, rr_costs[inode]);
        max_cost = std::max(max_cost, rr_costs[inode]);
    }
    if (min_cost == std::numeric_limits<float>::infinity()) min_cost = 0;
    if (max_cost == -std::numeric_limits<float>::infinity()) max_cost = 0;
    std::unique_ptr<vtr::ColorMap> cmap = std::make_unique<vtr::PlasmaColorMap>(min_cost, max_cost);

    //Draw the nodes in ascending order of value, this ensures high valued nodes
    //are not overdrawn by lower value ones (e.g-> when zoomed-out far)
    std::vector<int> nodes(device_ctx.rr_nodes.size());
    std::iota(nodes.begin(), nodes.end(), 0);
    auto cmp_ascending_cost = [&](int lhs_node, int rhs_node) {
        if (lowest_cost_first) {
            return rr_costs[lhs_node] > rr_costs[rhs_node];
        }
        return rr_costs[lhs_node] < rr_costs[rhs_node];
    };
    std::sort(nodes.begin(), nodes.end(), cmp_ascending_cost);

    for (int inode : nodes) {
        float cost = rr_costs[inode];
        RRNodeId rr_node = RRNodeId(inode);
        if (std::isnan(cost)) continue;

        ezgl::color color = to_ezgl_color(cmap->color(cost));

        switch (rr_graph.node_type(rr_node)) {
            case CHANX: //fallthrough
            case CHANY:
                draw_rr_chan(inode, color, g);
                if (with_edges) draw_rr_edges(inode, g);
                break;

            case IPIN: //fallthrough
                draw_rr_pin(inode, color, g);
                if (with_edges) draw_rr_edges(inode, g);
                break;
            case OPIN:
                draw_rr_pin(inode, color, g);
                if (with_edges) draw_rr_edges(inode, g);
                break;
            case SOURCE:
            case SINK:
                color.alpha *= 0.8;
                draw_rr_src_sink(inode, color, g);
                if (with_edges) draw_rr_edges(inode, g);
                break;
            default:
                break;
        }
    }

    draw_state->color_map = std::move(cmap);
}

static void draw_placement_macros(ezgl::renderer* g) {
    t_draw_state* draw_state = get_draw_state_vars();

    if (draw_state->show_placement_macros == DRAW_NO_PLACEMENT_MACROS) {
        return;
    }
    t_draw_coords* draw_coords = get_draw_coords_vars();

    auto& place_ctx = g_vpr_ctx.placement();
    for (size_t imacro = 0; imacro < place_ctx.pl_macros.size(); ++imacro) {
        const t_pl_macro* pl_macro = &place_ctx.pl_macros[imacro];

        //TODO: for now we just draw the bounding box of the macro, which is incorrect for non-rectangular macros...
        int xlow = std::numeric_limits<int>::max();
        int ylow = std::numeric_limits<int>::max();
        int xhigh = std::numeric_limits<int>::min();
        int yhigh = std::numeric_limits<int>::min();

        int x_root = OPEN;
        int y_root = OPEN;
        for (size_t imember = 0; imember < pl_macro->members.size();
             ++imember) {
            const t_pl_macro_member* member = &pl_macro->members[imember];

            ClusterBlockId blk = member->blk_index;

            if (imember == 0) {
                x_root = place_ctx.block_locs[blk].loc.x;
                y_root = place_ctx.block_locs[blk].loc.y;
            }

            int x = x_root + member->offset.x;
            int y = y_root + member->offset.y;

            xlow = std::min(xlow, x);
            ylow = std::min(ylow, y);
            xhigh = std::max(xhigh, x + physical_tile_type(blk)->width);
            yhigh = std::max(yhigh, y + physical_tile_type(blk)->height);
        }

        double draw_xlow = draw_coords->tile_x[xlow];
        double draw_ylow = draw_coords->tile_y[ylow];
        double draw_xhigh = draw_coords->tile_x[xhigh];
        double draw_yhigh = draw_coords->tile_y[yhigh];

        g->set_color(blk_RED);
        g->draw_rectangle({draw_xlow, draw_ylow},
                          {draw_xhigh, draw_yhigh});

        ezgl::color fill = blk_SKYBLUE;
        fill.alpha *= 0.3;
        g->set_color(fill);
        g->fill_rectangle({draw_xlow, draw_ylow},
                          {draw_xhigh, draw_yhigh});
    }
}

static void highlight_blocks(double x, double y) {
    t_draw_coords* draw_coords = get_draw_coords_vars();

    char msg[vtr::bufsize];
    ClusterBlockId clb_index = EMPTY_BLOCK_ID;
    auto& device_ctx = g_vpr_ctx.device();
    auto& cluster_ctx = g_vpr_ctx.clustering();
    auto& place_ctx = g_vpr_ctx.placement();

    /// determine block ///
    ezgl::rectangle clb_bbox;

    // iterate over grid x
    for (size_t i = 0; i < device_ctx.grid.width(); ++i) {
        if (draw_coords->tile_x[i] > x) {
            break; // we've gone to far in the x direction
        }
        // iterate over grid y
        for (size_t j = 0; j < device_ctx.grid.height(); ++j) {
            if (draw_coords->tile_y[j] > y) {
                break; // we've gone to far in the y direction
            }
            // iterate over sub_blocks
            const t_grid_tile* grid_tile = &device_ctx.grid[i][j];
            for (int k = 0; k < grid_tile->type->capacity; ++k) {
                clb_index = place_ctx.grid_blocks[i][j].blocks[k];
                if (clb_index != EMPTY_BLOCK_ID) {
                    clb_bbox = draw_coords->get_absolute_clb_bbox(clb_index,
                                                                  cluster_ctx.clb_nlist.block_type(clb_index));
                    if (clb_bbox.contains({x, y})) {
                        break;
                    } else {
                        clb_index = EMPTY_BLOCK_ID;
                    }
                }
            }
            if (clb_index != EMPTY_BLOCK_ID) {
                break; // we've found something
            }
        }
        if (clb_index != EMPTY_BLOCK_ID) {
            break; // we've found something
        }
    }

    if (clb_index == EMPTY_BLOCK_ID || clb_index == ClusterBlockId::INVALID()) {
        //Nothing found
        return;
    }

    VTR_ASSERT(clb_index != EMPTY_BLOCK_ID);

    // note: this will clear the selected sub-block if show_blk_internal is 0,
    // or if it doesn't find anything
    ezgl::point2d point_in_clb = ezgl::point2d(x, y) - clb_bbox.bottom_left();
    highlight_sub_block(point_in_clb, clb_index,
                        cluster_ctx.clb_nlist.block_pb(clb_index));

    if (get_selected_sub_block_info().has_selection()) {
        t_pb* selected_subblock = get_selected_sub_block_info().get_selected_pb();
        sprintf(msg, "sub-block %s (a \"%s\") selected",
                selected_subblock->name,
                selected_subblock->pb_graph_node->pb_type->name);
    } else {
        /* Highlight block and fan-in/fan-outs. */
        draw_highlight_blocks_color(cluster_ctx.clb_nlist.block_type(clb_index),
                                    clb_index);
        sprintf(msg, "Block #%zu (%s) at (%d, %d) selected.", size_t(clb_index),
                cluster_ctx.clb_nlist.block_name(clb_index).c_str(),
                place_ctx.block_locs[clb_index].loc.x,
                place_ctx.block_locs[clb_index].loc.y);
    }

    //If manual moves is activated, then user can select block from the grid.
    t_draw_state* draw_state = get_draw_state_vars();
    if (draw_state->manual_moves_state.manual_move_enabled) {
        draw_state->manual_moves_state.user_highlighted_block = true;
        if (!draw_state->manual_moves_state.manual_move_window_is_open) {
            draw_manual_moves_window(std::to_string(size_t(clb_index)));
        }
    }

    application.update_message(msg);
    application.refresh_drawing();
}
void set_net_alpha_value(GtkWidget* widget, gint /*response_id*/, gpointer /*data*/) {
    std::string fa(gtk_entry_get_text((GtkEntry*)widget));
    t_draw_state* draw_state = get_draw_state_vars();
    draw_state->net_alpha = std::stof(fa);
    application.refresh_drawing();
}

void set_net_alpha_value_with_enter(GtkWidget* widget, gint /*response_id*/, gpointer /*data*/) {
    std::string fa(gtk_entry_get_text((GtkEntry*)widget));
    t_draw_state* draw_state = get_draw_state_vars();
    draw_state->net_alpha = std::stof(fa);
    application.refresh_drawing();
}

float get_net_alpha() {
    t_draw_state* draw_state = get_draw_state_vars();
    return draw_state->net_alpha;
}

static void setup_default_ezgl_callbacks(ezgl::application* app) {
    // Connect press_proceed function to the Proceed button
    GObject* proceed_button = app->get_object("ProceedButton");
    g_signal_connect(proceed_button, "clicked", G_CALLBACK(ezgl::press_proceed),
                     app);

    // Connect press_zoom_fit function to the Zoom-fit button
    GObject* zoom_fit_button = app->get_object("ZoomFitButton");
    g_signal_connect(zoom_fit_button, "clicked",
                     G_CALLBACK(ezgl::press_zoom_fit), app);

    // Connect Pause button
    GObject* pause_button = app->get_object("PauseButton");
    g_signal_connect(pause_button, "clicked", G_CALLBACK(set_force_pause), app);

    // Connect Block Outline checkbox
    GObject* block_outline = app->get_object("blockOutline");
    g_signal_connect(block_outline, "toggled", G_CALLBACK(set_block_outline),
                     app);

    // Connect Block Text checkbox
    GObject* block_text = app->get_object("blockText");
    g_signal_connect(block_text, "toggled", G_CALLBACK(set_block_text), app);

    // Connect Clip Routing Util checkbox
    GObject* clip_routing = app->get_object("clipRoutingUtil");
    g_signal_connect(clip_routing, "toggled", G_CALLBACK(clip_routing_util),
                     app);

    // Connect Debug Button
    GObject* debugger = app->get_object("debugButton");
    g_signal_connect(debugger, "clicked", G_CALLBACK(draw_debug_window), NULL);
}

// Callback function for Block Outline checkbox
static void set_block_outline(GtkWidget* widget, gint /*response_id*/, gpointer /*data*/) {
    t_draw_state* draw_state = get_draw_state_vars();

    // assign corresponding bool value to draw_state->draw_block_outlines
    if (gtk_toggle_button_get_active((GtkToggleButton*)widget))
        draw_state->draw_block_outlines = true;
    else
        draw_state->draw_block_outlines = false;
    //redraw
    application.update_message(draw_state->default_message);
    application.refresh_drawing();
}

// Callback function for Block Text checkbox
static void set_block_text(GtkWidget* widget, gint /*response_id*/, gpointer /*data*/) {
    t_draw_state* draw_state = get_draw_state_vars();

    // assign corresponding bool value to draw_state->draw_block_text
    if (gtk_toggle_button_get_active((GtkToggleButton*)widget))
        draw_state->draw_block_text = true;
    else
        draw_state->draw_block_text = false;

    //redraw
    application.update_message(draw_state->default_message);
    application.refresh_drawing();
}

// Callback function for Clip Routing Util checkbox
static void clip_routing_util(GtkWidget* widget, gint /*response_id*/, gpointer /*data*/) {
    t_draw_state* draw_state = get_draw_state_vars();

    // assign corresponding bool value to draw_state->clip_routing_util
    if (gtk_toggle_button_get_active((GtkToggleButton*)widget))
        draw_state->clip_routing_util = true;
    else
        draw_state->clip_routing_util = false;

    //redraw
    application.update_message(draw_state->default_message);
    application.refresh_drawing();
}

// Callback function for NetMax Fanout checkbox
void net_max_fanout(GtkWidget* /*widget*/, gint /*response_id*/, gpointer /*data*/) {
    /* this is the callback function for runtime created net_max_fanout widget
     * which is written in button.cpp                                         */
    std::string button_name = "netMaxFanout";
    auto max_fanout = find_button(button_name.c_str());
    t_draw_state* draw_state = get_draw_state_vars();

    //set draw_state->draw_net_max_fanout to its corresponding value in the ui
    int new_value = gtk_spin_button_get_value_as_int(
        (GtkSpinButton*)max_fanout);
    draw_state->draw_net_max_fanout = new_value;

    //redraw
    application.refresh_drawing();
}

static void set_force_pause(GtkWidget* /*widget*/, gint /*response_id*/, gpointer /*data*/) {
    t_draw_state* draw_state = get_draw_state_vars();

    draw_state->forced_pause = true;
}

static void run_graphics_commands(std::string commands) {
    //A very simmple command interpreter for scripting graphics
    t_draw_state* draw_state = get_draw_state_vars();

    t_draw_state backup_draw_state = *draw_state;

    std::vector<std::vector<std::string>> cmds;
    for (std::string raw_cmd : vtr::split(commands, ";")) {
        cmds.push_back(vtr::split(raw_cmd));
    }

    for (auto& cmd : cmds) {
        VTR_ASSERT_MSG(cmd.size() > 0, "Expect non-empty graphics commands");

        for (auto& item : cmd) {
            VTR_LOG("%s ", item.c_str());
        }
        VTR_LOG("\n");

        if (cmd[0] == "save_graphics") {
            VTR_ASSERT_MSG(cmd.size() == 2,
                           "Expect filename after 'save_graphics'");

            auto name_ext = vtr::split_ext(cmd[1]);

            //Replace {i}  with the sequence number
            std::string name = vtr::replace_all(name_ext[0], "{i}",
                                                std::to_string(draw_state->sequence_number));

            save_graphics(/*extension=*/name_ext[1], /*filename=*/name);
            VTR_LOG("Saving to %s\n", std::string(name + name_ext[1]).c_str());

        } else if (cmd[0] == "set_macros") {
            VTR_ASSERT_MSG(cmd.size() == 2,
                           "Expect net draw state after 'set_macro'");
            draw_state->show_placement_macros = (e_draw_placement_macros)vtr::atoi(cmd[1]);
            VTR_LOG("%d\n", (int)draw_state->show_placement_macros);
        } else if (cmd[0] == "set_nets") {
            VTR_ASSERT_MSG(cmd.size() == 2,
                           "Expect net draw state after 'set_nets'");
            draw_state->show_nets = (e_draw_nets)vtr::atoi(cmd[1]);
            VTR_LOG("%d\n", (int)draw_state->show_nets);
        } else if (cmd[0] == "set_cpd") {
            VTR_ASSERT_MSG(cmd.size() == 2,
                           "Expect cpd draw state after 'set_cpd'");
            draw_state->show_crit_path = (e_draw_crit_path)vtr::atoi(cmd[1]);
            VTR_LOG("%d\n", (int)draw_state->show_crit_path);
        } else if (cmd[0] == "set_routing_util") {
            VTR_ASSERT_MSG(cmd.size() == 2,
                           "Expect routing util draw state after 'set_routing_util'");
            draw_state->show_routing_util = (e_draw_routing_util)vtr::atoi(
                cmd[1]);
            VTR_LOG("%d\n", (int)draw_state->show_routing_util);
        } else if (cmd[0] == "set_clip_routing_util") {
            VTR_ASSERT_MSG(cmd.size() == 2,
                           "Expect routing util draw state after 'set_routing_util'");
            draw_state->clip_routing_util = (bool)vtr::atoi(cmd[1]);
            VTR_LOG("%d\n", (int)draw_state->clip_routing_util);
        } else if (cmd[0] == "set_congestion") {
            VTR_ASSERT_MSG(cmd.size() == 2,
                           "Expect congestion draw state after 'set_congestion'");
            draw_state->show_congestion = (e_draw_congestion)vtr::atoi(cmd[1]);
            VTR_LOG("%d\n", (int)draw_state->show_congestion);
        } else if (cmd[0] == "set_draw_block_outlines") {
            VTR_ASSERT_MSG(cmd.size() == 2,
                           "Expect draw block outlines state after 'set_draw_block_outlines'");
            draw_state->draw_block_outlines = vtr::atoi(cmd[1]);
            VTR_LOG("%d\n", (int)draw_state->draw_block_outlines);
        } else if (cmd[0] == "set_draw_block_text") {
            VTR_ASSERT_MSG(cmd.size() == 2,
                           "Expect draw block text state after 'set_draw_block_text'");
            draw_state->draw_block_text = vtr::atoi(cmd[1]);
            VTR_LOG("%d\n", (int)draw_state->draw_block_text);
        } else if (cmd[0] == "set_draw_block_internals") {
            VTR_ASSERT_MSG(cmd.size() == 2,
                           "Expect draw state after 'set_draw_block_internals'");
            draw_state->show_blk_internal = vtr::atoi(cmd[1]);
            VTR_LOG("%d\n", (int)draw_state->show_blk_internal);
        } else if (cmd[0] == "set_draw_net_max_fanout") {
            VTR_ASSERT_MSG(cmd.size() == 2,
                           "Expect maximum fanout after 'set_draw_net_max_fanout'");
            draw_state->draw_net_max_fanout = vtr::atoi(cmd[1]);
            VTR_LOG("%d\n", (int)draw_state->draw_net_max_fanout);
        } else if (cmd[0] == "exit") {
            VTR_ASSERT_MSG(cmd.size() == 2, "Expect exit code after 'exit'");
            exit(vtr::atoi(cmd[1]));
        } else {
            VPR_ERROR(VPR_ERROR_DRAW,
                      vtr::string_fmt("Unrecognized graphics command '%s'",
                                      cmd[0].c_str())
                          .c_str());
        }
    }

    *draw_state = backup_draw_state; //Restor original draw state

    //Advance the sequence number
    ++draw_state->sequence_number;
}

/* This routine highlights the blocks affected in the latest move      *
 * It highlights the old and new locations of the moved blocks         *
 * It also highlights the moved block input and output terminals       *
 * Currently, it is used in placer debugger when breakpoint is reached */
void highlight_moved_block_and_its_terminals(
    const t_pl_blocks_to_be_moved& blocks_affected) {
    auto& cluster_ctx = g_vpr_ctx.clustering();

    //clear all selected blocks
    deselect_all();

    //highlight the input/output terminals of the moved block
    draw_highlight_blocks_color(
        cluster_ctx.clb_nlist.block_type(
            blocks_affected.moved_blocks[0].block_num),
        blocks_affected.moved_blocks[0].block_num);

    //highlight the old and new locations of the moved block
    clear_colored_locations();
    set_draw_loc_color(blocks_affected.moved_blocks[0].old_loc,
                       OLD_BLK_LOC_COLOR);
    set_draw_loc_color(blocks_affected.moved_blocks[0].old_loc,
                       NEW_BLK_LOC_COLOR);
}

// pass in an (x,y,subtile) location and the color in which it should be drawn.
// This overrides the color of any block placed in that location, and also applies if the location is empty.
void set_draw_loc_color(t_pl_loc loc, ezgl::color clr) {
    t_draw_state* draw_state = get_draw_state_vars();
    draw_state->colored_locations.push_back(std::make_pair(loc, clr));
}

// clear the colored_locations vector
void clear_colored_locations() {
    t_draw_state* draw_state = get_draw_state_vars();
    draw_state->colored_locations.clear();
}

// This routine takes in a (x,y) location.
// If the input loc is marked in colored_locations vector, the function will return true and the correspnding color is sent back in loc_color
// otherwise, the function returns false (the location isn't among the highlighted locations)
bool highlight_loc_with_specific_color(int x, int y, ezgl::color& loc_color) {
    t_draw_state* draw_state = get_draw_state_vars();

    //define a (x,y) location variable
    t_pl_loc curr_loc;
    curr_loc.x = x;
    curr_loc.y = y;

    //search for the current location in the vector of colored locations
    auto it = std::find_if(draw_state->colored_locations.begin(),
                           draw_state->colored_locations.end(),
                           [&curr_loc](const std::pair<t_pl_loc, ezgl::color>& vec_element) {
                               return (vec_element.first.x == curr_loc.x
                                       && vec_element.first.y == curr_loc.y);
                           });

    if (it != draw_state->colored_locations.end()) {
        /* found a colored location at the spot I am drawing *
         * (currently used for drawing the current move).    *
         * This overrides any block color.                   */
        loc_color = it->second;
        return true;
    }

    return false;
}

#endif /* NO_GRAPHICS */
