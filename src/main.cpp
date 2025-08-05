#include <SFML/Graphics.hpp>
#include <unordered_set>
#include <iostream>
#include <cmath>
#include <imgui.h>
#include <imgui-SFML.h>
#include <omp.h>

namespace conway
{

    struct Vec2i
    {
        int x{};
        int y{};

        [[nodiscard]]
        bool operator==(const Vec2i &o) const noexcept
        {
            return x == o.x && y == o.y;
        }
    };

    struct HashVec2i
    {
        [[nodiscard]]
        std::size_t operator()(const Vec2i &v) const noexcept
        {
            std::size_t h1{std::hash<int>{}(v.x)};
            std::size_t h2{std::hash<int>{}(v.y)};
            return h1 ^ (h2 << 1);
        }
    };

    class GameOfLife
    {
    public:
        explicit constexpr GameOfLife() noexcept = default;

        /**
         * @brief Return number of alive cells
         */
        [[nodiscard]]
        std::size_t get_cell_count() const noexcept
        {
            return m_alive_cells.size();
        }

        /**
         * @brief Return number of checked cells
         */
        [[nodiscard]]
        std::size_t get_potential_count() const noexcept
        {
            return m_neighbor_counts.size();
        }

        /**
         * @brief Return the set of alive cells
         */
        [[nodiscard]]
        const std::unordered_set<Vec2i, HashVec2i> &get_cells() const noexcept
        {
            return m_alive_cells;
        }

        [[nodiscard]]
        bool is_paused() const noexcept
        {
            return m_paused;
        }

        /**
         * @brief Pause/Unpause the simulation
         */
        void toggle_pause() noexcept
        {
            m_paused = !m_paused;
        }

        /**
         * @brief Reset the simulation
         */
        void cleanup() noexcept
        {
            m_alive_cells.clear();
            m_neighbor_counts.clear();
        }

        /**
         * @brief Add an alive cell on the given position
         *
         * @param v Cell position
         */
        void add_alive_cell(const Vec2i &v) noexcept
        {
            // Only update neighbors if cell was actually inserted
            if (m_alive_cells.insert(v).second)
            {
                update_neighbor_counts(v, 1);
            }
        }

        /**
         * @brief Remove an alive cell from the given position
         *
         * @param v Cell position
         */
        void remove_alive_cell(const Vec2i &v) noexcept
        {
            // Only update neighbors if cell was actually removed
            if (m_alive_cells.erase(v) > 0)
            {
                update_neighbor_counts(v, -1);
            }
        }

        /**
         * @brief Update the simulation
         */
        void update() noexcept
        {
            if (m_paused)
                return;

            std::unordered_set<Vec2i, HashVec2i> next_alive{};
            next_alive.reserve(m_alive_cells.size());

            std::unordered_map<Vec2i, int, HashVec2i> next_neighbor_counts{};
            next_neighbor_counts.reserve(m_neighbor_counts.size());

            // #pragma omp parallel for schedule(static)
            for (const auto &[cell, neighbor_count] : m_neighbor_counts)
            {
                bool is_alive{m_alive_cells.contains(cell)};
                bool will_be_alive{false};

                // Live cell survives with 2 or 3 neighbors
                if (is_alive)
                {
                    will_be_alive = (neighbor_count == 2 || neighbor_count == 3);
                }
                // Dead cell becomes alive with exactly 3 neighbors
                else
                {
                    will_be_alive = (neighbor_count == 3);
                }

                // Update neighbor counts for the next generation
                if (will_be_alive)
                {
                    next_alive.insert(cell);
                    update_neighbor_counts_map(next_neighbor_counts, cell, 1);
                }
            }

            m_alive_cells = std::move(next_alive);
            m_neighbor_counts = std::move(next_neighbor_counts);
        }

        void update_optimized() noexcept
        {
            if (m_paused)
                return;

            std::vector<std::unordered_set<Vec2i, HashVec2i>> thread_alive_sets{};
            std::vector<std::unordered_map<Vec2i, int, HashVec2i>> thread_neighbor_maps{};

            int num_threads{omp_get_max_threads()};
            thread_alive_sets.resize(num_threads);
            thread_neighbor_maps.resize(num_threads);

            std::vector<std::pair<Vec2i, int>> neighbor_pairs{};
            neighbor_pairs.reserve(m_neighbor_counts.size());
            for (const auto &pair : m_neighbor_counts)
            {
                neighbor_pairs.emplace_back(pair);
            }

#pragma omp parallel
            {
                int tid{omp_get_thread_num()};
                auto &alive_set{thread_alive_sets[tid]};
                auto &neighbor_map{thread_neighbor_maps[tid]};

#pragma omp for schedule(static)
                for (int i = 0; i < static_cast<int>(neighbor_pairs.size()); ++i)
                // for (auto it = m_neighbor_counts.begin(); it != m_neighbor_counts.end(); ++it)
                {
                    // const auto cell{it->first};
                    // int neighbor_count{it->second};
                    const auto &[cell, neighbor_count]{neighbor_pairs[i]};

                    bool is_alive{m_alive_cells.contains(cell)};
                    bool will_be_alive{(is_alive && (neighbor_count == 2 || neighbor_count == 3)) || (!is_alive && neighbor_count == 3)};

                    if (will_be_alive)
                    {
                        alive_set.insert(cell);
                        update_neighbor_counts_map(neighbor_map, cell, 1);
                    }
                }
            }

            // Fuse
            std::unordered_set<Vec2i, HashVec2i> next_alive{};
            std::unordered_map<Vec2i, int, HashVec2i> next_neighbor_counts{};

            for (auto &alive_set : thread_alive_sets)
            {
                next_alive.insert(alive_set.begin(), alive_set.end());
            }

            for (auto &local_map : thread_neighbor_maps)
            {
                for (auto &[key, value] : local_map)
                {
                    next_neighbor_counts[key] += value;
                }
            }

            m_alive_cells = std::move(next_alive);
            m_neighbor_counts = std::move(next_neighbor_counts);
        }

    private:
        /**
         * @brief Update neighbor counts when adding/removing a cell
         *
         * @param cell The cell being added/removed
         * @param delta +1 for adding, -1 for removing
         */
        void update_neighbor_counts(const Vec2i &cell, int delta) noexcept
        {
            // Cache friendly
            static constexpr std::array<Vec2i, 8> neighbor_offsets{{{-1, -1}, {-1, 0}, {-1, 1}, {0, -1}, {0, 1}, {1, -1}, {1, 0}, {1, 1}}};

            for (const auto &offset : neighbor_offsets)
            {
                Vec2i neighbor{cell.x + offset.x, cell.y + offset.y};

                auto it{m_neighbor_counts.find(neighbor)};
                if (it != m_neighbor_counts.end())
                {
                    it->second += delta;
                    // Remove entries with zero neighbors to keep map small
                    if (it->second == 0)
                    {
                        m_neighbor_counts.erase(it);
                    }
                }
                else if (delta > 0)
                {
                    // Only add new entries when incrementing
                    m_neighbor_counts[neighbor] = delta;
                }
            }
        }

        /**
         * @brief Update neighbor counts in a given map
         */
        void update_neighbor_counts_map(std::unordered_map<Vec2i, int, HashVec2i> &counts,
                                        const Vec2i &cell, int delta) noexcept
        {
            static constexpr std::array<Vec2i, 8> neighbor_offsets{{{-1, -1}, {-1, 0}, {-1, 1}, {0, -1}, {0, 1}, {1, -1}, {1, 0}, {1, 1}}};

            for (const auto &offset : neighbor_offsets)
            {
                Vec2i neighbor{cell.x + offset.x, cell.y + offset.y};
                counts[neighbor] += delta;
            }
        }

    private:
        bool m_paused{false};
        std::unordered_set<Vec2i, HashVec2i> m_alive_cells{};
        std::unordered_map<Vec2i, int, HashVec2i> m_neighbor_counts{};
    };

}

/**
 * @brief Convert a set to vertices
 *
 * @param positions Alive cells positions
 */
[[nodiscard]]
sf::VertexArray set_to_vertex_array(const std::unordered_set<conway::Vec2i, conway::HashVec2i> &positions) noexcept
{
    constexpr float size{1.0f};
    constexpr float hsize{0.5f * size};
    constexpr int vertex_per_cell{6};
    sf::VertexArray triangles{sf::PrimitiveType::Triangles, positions.size() * vertex_per_cell};
    std::vector<conway::Vec2i> positions_vec(positions.begin(), positions.end());

#pragma omp parallel for schedule(static)
    for (int i = 0; i < static_cast<int>(positions.size()); ++i)
    {
        const auto &pos{positions_vec[i]};
        float x{static_cast<float>(pos.x) * size};
        float y{static_cast<float>(pos.y) * size};
        std::size_t base{static_cast<std::size_t>(vertex_per_cell * i)};

        sf::Vector2f topLeft{x - hsize, y - hsize};
        sf::Vector2f topRight{x + hsize, y - hsize};
        sf::Vector2f bottomRight{x + hsize, y + hsize};
        sf::Vector2f bottomLeft{x - hsize, y + hsize};

        triangles[base + 0] = sf::Vertex(topLeft, sf::Color::White);
        triangles[base + 1] = sf::Vertex(topRight, sf::Color::White);
        triangles[base + 2] = sf::Vertex(bottomRight, sf::Color::White);

        triangles[base + 3] = sf::Vertex(bottomRight, sf::Color::White);
        triangles[base + 4] = sf::Vertex(bottomLeft, sf::Color::White);
        triangles[base + 5] = sf::Vertex(topLeft, sf::Color::White);
    }

    return triangles;
}

int main()
{
    srand(clock());
    conway::GameOfLife gol{};

    sf::RenderWindow window(sf::VideoMode({1280, 720}), "Infinite Conway Game Of Life");
    sf::View view{{0.0f, 0.0f}, {500.0f, 250.0f}};
    sf::Clock delta_clock{};
    sf::Clock input_clock{};
    sf::Clock profiling_clock{};
    window.setView(view);

    constexpr float zoom_factor{0.1f};
    float camera_spd{0.1f};
    int simu_time{};
    int draw_time{};
    float input_dt{};
    bool use_optimization{false};

    if (!ImGui::SFML::Init(window))
    {
        std::cerr << "Could not initialize ImGui\n";
        return 1;
    }

    while (window.isOpen())
    {
        float view_zoom{1.0f};
        sf::View new_view{window.getView()};
        sf::Vector2f new_view_center{new_view.getCenter()};
        sf::Vector2f dpos{};
        input_dt = input_clock.restart().asSeconds();

        while (const std::optional event = window.pollEvent())
        {
            ImGui::SFML::ProcessEvent(window, *event);

            if (ImGui::GetIO().WantCaptureMouse)
            {
                continue;
            }

            if (event->is<sf::Event::Closed>())
            {
                window.close();
            }

            if (const auto *key_pressed = event->getIf<sf::Event::KeyPressed>())
            {
                /* Close window */
                if (key_pressed->scancode == sf::Keyboard::Scancode::Escape)
                {
                    window.close();
                }

                /* Pause with P*/
                if (key_pressed->scancode == sf::Keyboard::Scancode::P)
                {
                    gol.toggle_pause();
                }

                /* Reset simulation */
                if (key_pressed->scancode == sf::Keyboard::Scancode::R)
                {
                    gol.cleanup();
                }

                /* Toggle optimization */
                if (key_pressed->scancode == sf::Keyboard::Scancode::O)
                {
                    use_optimization = !use_optimization;
                }
            }

            /* Zoom with wheel */
            if (const auto *mouse_wheel_scrolled = event->getIf<sf::Event::MouseWheelScrolled>())
            {
                if (mouse_wheel_scrolled->wheel == sf::Mouse::Wheel::Vertical)
                {
                    view_zoom *= (1.0f + zoom_factor * mouse_wheel_scrolled->delta);
                    camera_spd *= view_zoom;
                }
            }
        }

        /* Add on cell on screen with left click */
        if (sf::Mouse::isButtonPressed(sf::Mouse::Button::Left) && !ImGui::GetIO().WantCaptureMouse)
        {
            sf::Vector2i mouse_local{sf::Mouse::getPosition(window)};
            sf::Vector2f world_pos{window.mapPixelToCoords(mouse_local)};
            gol.add_alive_cell(conway::Vec2i{static_cast<int>(std::round(world_pos.x)), static_cast<int>(std::round(world_pos.y))});
        }

        /* Add a bunch of cells on screen with right click */
        if (sf::Mouse::isButtonPressed(sf::Mouse::Button::Right) && !ImGui::GetIO().WantCaptureMouse)
        {
            sf::Vector2i mouse_local{sf::Mouse::getPosition(window)};
            sf::Vector2f world_pos{window.mapPixelToCoords(mouse_local)};
            for (int i = -50; i <= 50; i++)
            {
                for (int j = -50; j < 50; j++)
                {
                    if (rand() % 2)
                    {
                        gol.add_alive_cell(conway::Vec2i{static_cast<int>(std::round(world_pos.x) + i), static_cast<int>(std::round(world_pos.y) + j)});
                    }
                }
            }
        }

        /* Move in world with WASD - ZQSD */
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scancode::W))
        {
            dpos.y += -camera_spd * input_dt;
        }

        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scancode::A))
        {
            dpos.x += -camera_spd * input_dt;
        }

        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scancode::S))
        {
            dpos.y += camera_spd * input_dt;
        }

        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scancode::D))
        {
            dpos.x += camera_spd * input_dt;
        }

        /* Uodate ImGui */
        ImGui::SFML::Update(window, delta_clock.restart());
        ImGui::Begin("Game Of Life");

        if (ImGui::BeginTabBar("Stats"))
        {
            if (ImGui::BeginTabItem("Info"))
            {
                ImGui::Text("Active cells : %zu", gol.get_cell_count());
                ImGui::Text("Potential cells : %zu", gol.get_potential_count());
                ImGui::Text("Simulation time : %dms", simu_time);
                ImGui::Text("Draw time : %dms", draw_time);
                ImGui::Text("Paused: %s", gol.is_paused() ? "ON" : "OFF");
                ImGui::Text("Optimization: %s", use_optimization ? "ON" : "OFF");
                if (ImGui::Button("Pause"))
                {
                    gol.toggle_pause();
                }
                if (ImGui::Button("Reset"))
                {
                    gol.cleanup();
                }

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Inputs"))
            {
                ImGui::Text("WASD: Move camera");
                ImGui::Text("P: Toggle pause");
                ImGui::Text("R: Reset");
                ImGui::Text("O: Toggle Optimization");
                ImGui::Text("LMB: Spawn one cell");
                ImGui::Text("RMB: Spawn multiple cells");
                ImGui::Text("Mouse Wheel: Zoom");
                ImGui::Text("ESC: Close window");
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();

        /* Update view */
        new_view_center = dpos.lengthSquared() != 0 ? new_view_center + dpos.normalized() : new_view_center;
        new_view.setCenter(new_view_center);
        new_view.zoom(view_zoom);
        window.setView(new_view);

        /* Update simu */
        profiling_clock.restart();
        if (use_optimization)
        {
            gol.update_optimized();
        }
        else
        {
            gol.update();
        }
        simu_time = profiling_clock.restart().asMilliseconds();

        /* Draw */
        profiling_clock.restart();
        window.clear(sf::Color::Black);
        window.draw(set_to_vertex_array(gol.get_cells()));
        ImGui::SFML::Render(window);
        window.display();
        draw_time = profiling_clock.restart().asMilliseconds();
    }
}
