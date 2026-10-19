#include "grip/settings.hpp"
#include <yaml-cpp/yaml.h>
#include <set>
#include <sstream>
#include <cmath>
#include <iostream>

namespace grip
{

    static bool valid_device(const std::string &s)
    {
        static const std::set<std::string> k{"THUMB", "INDEX_FINGER", "MIDDLE_FINGER", "RING_FINGER", "PINKY", "WRIST"};
        return k.count(s) > 0;
    }
    static auto rt(const YAML::Node &n) { return Timing{n["tseg1"].as<int>(), n["tseg2"].as<int>(), n["sjw"].as<int>(), n["brp"].as<int>()}; }

    static Point3D yaml_point(const YAML::Node &p)
    {
        Point3D pt;
        if (p.IsSequence() && p.size() >= 2)
        {
            pt.x = p[0].as<double>(0.0);
            pt.y = p[1].as<double>(0.0);
            pt.z = p.size() > 2 ? p[2].as<double>(0.0) : 0.0; // 2D point -> z = 0
        }
        return pt;
    }

    AppSettings load_app_settings(const std::string &yaml_path)
    {
        YAML::Node y = YAML::LoadFile(yaml_path);
        AppSettings s;

        if (auto n = y["can_device"])
        {
            s.can_device.device_type = n["device_type"].as<uint32_t>(s.can_device.device_type);
            s.can_device.device_index = n["device_index"].as<uint32_t>(s.can_device.device_index);
            s.can_device.channel_index = n["channel_index"].as<uint32_t>(s.can_device.channel_index);
            s.can_device.core_clock_hz = n["core_clock_hz"].as<uint32_t>(s.can_device.core_clock_hz);
            s.can_device.termination_enabled = n["termination_enabled"].as<bool>(s.can_device.termination_enabled);
            if (auto t = n["bit_timing"])
            {
                s.can_device.bit_timing.arbitration = rt(t["arbitration"]);
                s.can_device.bit_timing.data_phase = rt(t["data_phase"]);
            }
        }
        if (auto n = y["models"])
            for (const auto &it : n)
                s.models.push_back(it.as<std::string>());
        validate_settings(s, yaml_path);
        return s;
    }

    DeviceSettings load_device_settings(const std::string &yaml_path)
    {
        YAML::Node y = YAML::LoadFile(yaml_path);
        DeviceSettings device;

        if (auto n = y["type"])
        {
            device.device_type = n.as<std::string>() == "hand" ? 0 : (n.as<std::string>() == "linear_actuator" ? 1 : 2);
        }

        if (auto hardware = y["hardware_version"])
            device.hardware_version = hardware.as<int32_t>(device.hardware_version);

        if (auto software = y["software_version"])
            device.software_version = software.as<int32_t>(device.software_version);

        return device;
    }

    HandSettings load_hand_settings(const std::string &yaml_path)
    {
        YAML::Node y = YAML::LoadFile(yaml_path);
        HandSettings hand;

        std::vector<std::string> errs;
        auto add = [&](std::string m)
        { errs.push_back(yaml_path + " — " + m); };

        if (auto n = y["hand_struct"])
        {
            for (auto m = n.begin(); m != n.end(); ++m)
            {
                ModuleStruct ms;

                int counter = 0;
                for (const auto joint : m->second["joints"])
                {
                    JointInfo j;
                    j.name = joint.as<std::string>();
                    if (counter >= m->second["route"].size())
                        add("Route size does not match joint number");
                    if (counter >= m->second["hall_route"].size())
                        add("Hall route size does not match joint number");
                    if (counter >= m->second["joint_feedback_map"].size())
                        add("Map size does not match joint number");
                    if (counter >= m->second["joint_ids"].size())
                        add("number of Ids does not match joint number");
                    if (counter >= m->second["joint_feedback"].size())
                        add("number of feedback states does not match joint number");

                    if (auto dof_id = m->second["joint_ids"][counter])
                        j.id = dof_id.as<int>(j.id);

                    if (j.id >= 0) {
                        if (auto route = m->second["route"][counter])
                        {
                            if (auto min = route["min"])
                                j.route.first = min.as<float_t>(j.route.first);
                            if (auto max = route["max"])
                                j.route.second = max.as<float_t>(j.route.second);
                        }
                        if (auto hall_route = m->second["hall_route"][counter])
                        {
                            if (auto min = hall_route["min"])
                                j.hall_route.first = min.as<float_t>(j.hall_route.first);
                            if (auto max = hall_route["max"])
                                j.hall_route.second = max.as<float_t>(j.hall_route.second);
                        }
                        if (j.route.first >= j.route.second)
                            add("Route range error: " + j.name + "\n");
                        if (j.hall_route.first >= j.hall_route.second)
                            add("Route range error: " + j.name + "\n");
                    }

                    if (auto jf = m->second["joint_feedback"][counter])
                        j.have_joint_feedback = jf.as<bool>(j.have_joint_feedback);
                    if (j.have_joint_feedback)
                    {
                        if (auto map = m->second["joint_feedback_map"][counter])
                        {
                            for (auto v : map)
                                j.joint_feedback_map.push_back(v.as<float>());
                        }
                    }

                    // if (j.dof_joint_map.size() != ms.joints.size())
                    //     add("Map size for " + d.name + " dose not match the number of joints\n");

                    ms.push_back(j);
                    counter += 1;
                }

                if (ms.size() <= 0)
                    add("No joint info for " + m->first.as<std::string>() + "\n");

                hand.structs[m->first.as<std::string>()] = std::move(ms);
                if (!valid_device(m->first.as<std::string>()))
                    add("Invalid module: "+ m->first.as<std::string>() + "\n");
            }
        }

        if (auto p = y["control_policy"])
        {
            for (auto const policy : p)
            {
                hand.control_policies.push_back(policy.as<std::string>());
            }
        }
        if (hand.control_policies.size() <= 0)
            add("No finger control policy available.\n");

        if (auto p = y["wrist_control_policy"])
        {
            for (auto const policy : p)
            {
                hand.wrist_control.push_back(policy.as<std::string>());
            }
        }
        if (hand.wrist_control.size() <= 0)
            add("No wrist control policy available.\n");

        if (auto p = y["urdf_file"])
            hand.urdf_file = p.as<std::string>();

        if (auto t = y["tactile_sensor"])
            hand.tactile_sensor = t.as<bool>(hand.tactile_sensor);

        // tactiles: per-sensor config. Each entry may inherit a shared layout
        // ("tactile_layouts") through the YAML merge anchor "<<: *name"; the
        // anchor content is resolved here so the struct stays flat.
        if (auto tactiles = y["tactiles"])
        {
            for (auto it = tactiles.begin(); it != tactiles.end(); ++it)
            {
                TactileConfig cfg;
                cfg.name = it->first.as<std::string>();
                // Take the node by value: holding a reference while iterating
                // its children trips yaml-cpp's node ownership tracking.
                YAML::Node n = it->second;

                // Merge-key resolution: explicit keys win, otherwise the value
                // comes from the referenced layout.
                auto field = [&n](const char *key) -> YAML::Node
                {
                    if (n[key])
                        return n[key];
                    if (n["<<"])
                        return n["<<"][key];
                    return YAML::Node();
                };

                if (auto d = field("dimension"))
                    cfg.coord_dimension = d.as<int>(2); // 2 - 2D, 3 - 3D
                if (auto pn = field("point_num"))
                    cfg.point_num = pn.as<int>(cfg.point_num);
                if (auto coords = field("point_coords"))
                {
                    for (const auto &pc : coords)
                        cfg.point_coord.push_back(yaml_point(pc));
                }
                if (auto link = field("related_link"))
                    cfg.related_link_name = link.as<std::string>();
                if (auto off = field("link_offset"))
                    cfg.link_offset = yaml_point(off);

                hand.tactiles.push_back(std::move(cfg));
            }
        }

        validate_settings(hand, yaml_path);

        throw_errors(errs);

        return hand;
    }

    ActuatorSettings load_actuator_settings(const std::string &yaml_path)
    {
        YAML::Node y = YAML::LoadFile(yaml_path);
        ActuatorSettings act;

        if (auto r = y["route"])
        {
            act.route.first = r["min"].as<float_t>(act.route.first);
            act.route.second = r["max"].as<float_t>(act.route.second);
        }

        if (auto mc = y["max_current"])
        {
            act.max_current = mc.as<int32_t>(act.max_current);
        }

        if (auto mv = y["max_velocity"])
        {
            act.max_vel = mv.as<int32_t>(act.max_vel);
        }

        if (auto p = y["psi_f"])
        {
            act.psi_f = p.as<float_t>(act.psi_f);
        }

        if (auto p = y["n_pole_pairs"])
        {
            act.n_pole_pairs = p.as<float_t>(act.n_pole_pairs);
        }

        if (auto p = y["device_id"])
        {
            act.device_id = p.as<float_t>(act.device_id);
        }

        return act;
    }

    void validate_settings(const AppSettings &s, const std::string &path)
    {
        std::vector<std::string> errs;
        auto add = [&](std::string m)
        { errs.push_back(path + " — " + m); };
        if (s.models.empty())
            add("models list cannot be empty");
        // else
        // {
        //     std::set<std::string> uniq;
        //     for (size_t i = 0; i < s.models.size(); ++i)
        //     {
        //         if (!valid_device(s.models[i]))
        //             add("models[" + std::to_string(i) + "] invalid: " + s.models[i]);
        //         if (!uniq.insert(s.models[i]).second)
        //             add("duplicate joint name: " + s.models[i]);
        //     }
        // }

        auto check_t = [&](const char *w, const Timing &t)
        {
            if (t.tseg1 < 0 || t.tseg2 < 0 || t.sjw < 0 || t.brp < 0)
                add(std::string("can_device.bit_timing.") + w + ": fields must be positive");
            if (t.tseg1 > 31 || t.tseg2 > 15 || t.sjw > 7 || t.brp > 1024)
                add(std::string("can_device.bit_timing.") + w + ": out of typical range");
        };
        check_t("arbitration", s.can_device.bit_timing.arbitration);
        check_t("data_phase", s.can_device.bit_timing.data_phase);
        if (s.can_device.core_clock_hz < 1'000'000 || s.can_device.core_clock_hz > 200'000'000)
            add("can_device.core_clock_hz out of range");

        throw_errors(errs);
    }

    void validate_settings(const HandSettings &s, const std::string &path)
    {
        std::vector<std::string> errs;
        auto add = [&](std::string m)
        { errs.push_back(path + " — " + m); };

        if (s.tactile_sensor)
        {
            if (s.tactiles.empty())
                add("tactile_sensor enabled but tactiles is empty");
            for (size_t i = 0; i < s.tactiles.size(); ++i)
            {
                const TactileConfig &tc = s.tactiles[i];
                if (tc.name.empty())
                    add("tactile[" + std::to_string(i) + "] missing name");
                if (tc.coord_dimension != 2 && tc.coord_dimension != 3)
                    add("tactile[" + std::to_string(i) + "] dimension must be 2 or 3");
                if (tc.point_num <= 0)
                    add("tactile[" + std::to_string(i) + "] point_num must be positive");
                if (tc.point_coord.size() != static_cast<size_t>(tc.point_num))
                    add("tactile[" + std::to_string(i) + "] point_coord size does not match point_num");
            }
        }

        throw_errors(errs);
    }

    void throw_errors(std::vector<std::string> errs)
    {
        if (!errs.empty())
        {
            std::ostringstream oss;
            oss << "Invalid configuration:\n";
            for (auto &e : errs)
                oss << "  - " << e << "\n";
            throw SettingsError(oss.str());
        }
    }

} // namespace grip
