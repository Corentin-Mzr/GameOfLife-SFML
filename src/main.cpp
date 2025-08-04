#include <SFML/Graphics.hpp>
#include <unordered_set>
#include <iostream>
#include <cmath>
#include "imgui.h"
#include "imgui-SFML.h"

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
            return m_set_active_next.size();
        }

        /**
         * @brief Return number of checked cells
         */
        [[nodiscard]]
        std::size_t get_potential_count() const noexcept
        {
            return m_set_potential_next.size();
        }

        /**
         * @brief Return the set of alive cells
         */
        [[nodiscard]]
        const std::unordered_set<Vec2i, HashVec2i> &get_cells() const noexcept
        {
            return m_set_active;
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
            m_set_active.clear();
            m_set_active_next.clear();
            m_set_potential.clear();
            m_set_potential_next.clear();
        }

        /**
         * @brief Add an alive cell on the given position
         * 
         * @param v Cell position
         */
        void add_alive_cell(const Vec2i &v) noexcept
        {
            m_set_active_next.insert(v);
            m_set_active.insert(v);

            for (int y = -1; y <= 1; ++y)
            {
                for (int x = -1; x <= 1; ++x)
                {
                    m_set_potential_next.insert(Vec2i{v.x + x, v.y + y});
                }
            }
        }

        /**
         * @brief Update the simulation
         */
        void update() noexcept
        {
            if (m_paused)
                return;

            m_set_active = m_set_active_next;
            m_set_active_next.clear();
            m_set_active_next.reserve(m_set_active.size());

            m_set_potential = m_set_potential_next;
            m_set_potential_next = m_set_active;

            for (const auto &c : m_set_potential)
            {
                int neighbors{};
                for (int y = -1; y <= 1; ++y)
                {
                    for (int x = -1; x <= 1; ++x)
                    {
                        if (x == 0 && y == 0)
                            continue;
                        neighbors += get_cell_state(Vec2i{c.x + x, c.y + y});
                    }
                }

                // Cell is currently alive, check its state next epoch
                if (get_cell_state(c))
                {
                    // Die from isolation or overpopulation
                    if (neighbors < 2 || neighbors > 3)
                    {
                        // Neighbors are stimulated for next epoch
                        for (int y = -1; y <= 1; ++y)
                        {
                            for (int x = -1; x <= 1; ++x)
                            {
                                m_set_potential_next.insert(Vec2i{c.x + x, c.y + y});
                            }
                        }
                    }
                    else
                    {
                        m_set_active_next.insert(c);
                    }
                }
                // Cell currently is dead, check if it's alive next epoch
                else
                {
                    if (neighbors == 3)
                    {
                        // Spawn cell
                        m_set_active_next.insert(c);

                        // Neighbors are stimulated for next epoch
                        for (int y = -1; y <= 1; ++y)
                        {
                            for (int x = -1; x <= 1; ++x)
                            {
                                m_set_potential_next.insert(Vec2i{c.x + x, c.y + y});
                            }
                        }
                    }
                }
            }
        }

    private:
        /**
         * @brief Return true if cell is alive
         * 
         * @param v Cell position
         */
        [[nodiscard]]
        bool get_cell_state(const Vec2i &v) const noexcept
        {
            return m_set_active.contains(v);
        }

    private:
        bool m_paused{false};
        std::unordered_set<Vec2i, HashVec2i> m_set_active{};
        std::unordered_set<Vec2i, HashVec2i> m_set_active_next{};
        std::unordered_set<Vec2i, HashVec2i> m_set_potential{};
        std::unordered_set<Vec2i, HashVec2i> m_set_potential_next{};
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
    sf::VertexArray triangles(sf::PrimitiveType::Triangles, positions.size() * 6);

    std::size_t i{};
    for (const auto &pos : positions)
    {
        float x = static_cast<float>(pos.x) * size;
        float y = static_cast<float>(pos.y) * size;

        sf::Vector2f topLeft(x - hsize, y - hsize);
        sf::Vector2f topRight(x + hsize, y - hsize);
        sf::Vector2f bottomRight(x + hsize, y + hsize);
        sf::Vector2f bottomLeft(x - hsize, y + hsize);

        triangles[i++] = (sf::Vertex(topLeft, sf::Color::White));
        triangles[i++] = (sf::Vertex(topRight, sf::Color::White));
        triangles[i++] = (sf::Vertex(bottomRight, sf::Color::White));

        triangles[i++] = (sf::Vertex(bottomRight, sf::Color::White));
        triangles[i++] = (sf::Vertex(bottomLeft, sf::Color::White));
        triangles[i++] = (sf::Vertex(topLeft, sf::Color::White));
    }

    return triangles;
}

int main()
{
    srand(clock());

    conway::GameOfLife gol{};

    sf::RenderWindow window(sf::VideoMode({1280, 720}), "Infinite Conway Game Of Life");
    sf::View view{{0.0f, 0.0f}, {400.0f, 200.0f}};
    window.setView(view);
    sf::Clock delta_clock{};
    sf::Clock input_clock{};

    constexpr float zoom_factor{0.1f};
    float camera_spd{0.1f};
    int simu_time{};
    int draw_time{};
    float input_dt{};

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

        while (const std::optional event = window.pollEvent())
        {
            ImGui::SFML::ProcessEvent(window, *event);

            if (event->is<sf::Event::Closed>())
                window.close();

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
            }

            /* Add on cell on screen with left click */
            if (sf::Mouse::isButtonPressed(sf::Mouse::Button::Left))
            {
                sf::Vector2i mouse_local{sf::Mouse::getPosition(window)};
                sf::Vector2f world_pos{window.mapPixelToCoords(mouse_local)};
                gol.add_alive_cell(conway::Vec2i{static_cast<int>(std::round(world_pos.x)), static_cast<int>(std::round(world_pos.y))});
            }

            /* Add a bunch of cells on screen with right click */
            if (sf::Mouse::isButtonPressed(sf::Mouse::Button::Right))
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

        /* Move in world with WASD - ZQSD */
        input_dt = input_clock.restart().asSeconds();

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
                ImGui::Text("Active cells : %d", gol.get_cell_count());
                ImGui::Text("Potential cells : %d", gol.get_potential_count());
                ImGui::Text("Simulation time : %dms", simu_time);
                ImGui::Text("Draw time : %dms", draw_time);
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
        sf::Clock clk{};
        gol.update();
        const auto &cells{gol.get_cells()};
        simu_time = clk.restart().asMilliseconds();

        /* Draw */
        clk.restart();
        window.clear(sf::Color::Black);
        window.draw(set_to_vertex_array(cells));
        ImGui::SFML::Render(window);
        window.display();
        draw_time = clk.restart().asMilliseconds();
    }
}
