#include <future>
#include <system_error>

#include <sys/types.h>
#include <sys/wait.h>

#include <pfq/pfq.hpp>

#include "yats.hpp"

using namespace yats;

const std::string DEV("eth0");

auto g = Group("PFQ")

    .Single("default_ctor_dtor", []
    {
        pfq::socket x;
        Assert(x.id(), is_equal_to(-1));
    })

    .Single("move_ctor", []
    {
        pfq::socket x(64);
        pfq::socket y(std::move(x));

        Assert(x.fd(), is_equal_to(-1));
        Assert(y.fd(), is_not_equal_to(-1));
    })


    .Single("assign_move_oper", []
    {
        pfq::socket x(64);
        pfq::socket y;
        y = std::move(x);

        Assert(x.fd(), is_equal_to(-1));
        Assert(y.fd(), is_not_equal_to(-1));
    })


    .Single("swap", []
    {
        pfq::socket x(64);
        pfq::socket y;
        x.swap(y);

        Assert(x.fd(), is_equal_to(-1));
        Assert(y.fd(), is_not_equal_to(-1));
    })


    .Single("open_close", []
    {
        pfq::socket x;
        x.open(pfq::group_policy::undefined, 64);

        Assert(x.fd(), is_not_equal_to(-1));
        Assert(x.id(), is_not_equal_to(-1));
        AssertThrow( x.open(pfq::group_policy::undefined, 128) );

        x.close();
        Assert(x.fd(), is_equal_to(-1));
    })


    .Single("enable_disable", []
    {
        pfq::socket x;

        AssertThrow(x.enable());
        AssertThrow(x.disable());

        x.open(pfq::group_policy::undefined, 64);

        x.enable();

        Assert(x.mem_addr());

        x.disable();

        Assert(x.mem_addr() == nullptr);
    })


    .Single("enabled", []
    {
        pfq::socket x;
        Assert(x.enabled(), is_equal_to(false));
        x.open(pfq::group_policy::undefined, 64);
        Assert(x.enabled(), is_equal_to(false));
        x.enable();
        Assert(x.enabled(), is_equal_to(true));
    })


    .Single("ifindex", []
    {
        pfq::socket x;
        AssertThrow(pfq::ifindex(1, "lo"));

        x.open(pfq::group_policy::undefined, 64);
        Assert( pfq::ifindex(x.fd(), "lo"), is_not_equal_to(-1));
    })


    .Single("timestamp", []
    {
        pfq::socket x;
        AssertThrow(x.timestamp_enable(true));
        AssertThrow(x.timestamp_enabled());

        x.open(pfq::group_policy::undefined, 64);
        x.timestamp_enable(true);

        Assert(x.timestamp_enabled(), is_equal_to(true));
    })


    .Single("caplen", []
    {
        pfq::socket x;
        AssertThrow(x.caplen(64));
        AssertThrow(x.caplen());

        x.open(pfq::group_policy::undefined, 64);
        x.caplen(128);

        Assert(x.caplen(), is_equal_to(128UL));

        x.enable();
        AssertThrow(x.caplen(64));
        x.disable();

        x.caplen(64);
        Assert(x.caplen(), is_equal_to(64UL));
    })


    .Single("maxlen", []
    {
        pfq::socket x;
        AssertThrow(x.maxlen());

        x.open(pfq::group_policy::undefined, 64);

        Assert(x.maxlen(), is_equal_to(1514UL));
    })

    .Single("rx_slots", []
    {
        pfq::socket x;
        AssertThrow(x.rx_slots(14));
        AssertThrow(x.rx_slots());

        x.open(pfq::group_policy::undefined, 64);
        x.rx_slots(1024);

        Assert(x.rx_slots(), is_equal_to(1024UL));

        x.enable();
        AssertThrow(x.rx_slots(4096));
        x.disable();

        x.rx_slots(4096);
        Assert(x.rx_slots(), is_equal_to(4096UL));
    })


    .Single("rx_slot_size", []
    {
        pfq::socket x;
        AssertThrow(x.rx_slot_size());

        x.open(pfq::group_policy::undefined, 64);

        auto size = 64 + sizeof(pfq_pkthdr);
        Assert(x.rx_slot_size(), is_equal_to(size));
    })

    .Single("tx_slots", []
    {
        pfq::socket x;
        AssertThrow(x.tx_slots(14));
        AssertThrow(x.tx_slots());

        x.open(pfq::group_policy::undefined, 64);
        x.tx_slots(1024);

        Assert(x.tx_slots(), is_equal_to(1024UL));

        x.enable();
        AssertThrow(x.tx_slots(4096));
        x.disable();

        x.tx_slots(4096);
        Assert(x.tx_slots(), is_equal_to(4096UL));
    })


    .Single("bind_device", []
    {
        pfq::socket x;
        AssertThrow(x.bind(DEV.c_str()));

        x.open(pfq::group_policy::shared, 64);

        AssertThrow(x.bind("unknown"));
        x.bind(DEV.c_str());

        AssertThrow(x.bind_group(11, DEV.c_str()));
    })


    .Single("unbind_device", []
    {
        pfq::socket x;
        AssertThrow(x.unbind(DEV.c_str()));

        x.open(pfq::group_policy::shared, 64);

        AssertThrow(x.unbind("unknown"));
        x.unbind(DEV.c_str());

        AssertThrow(x.unbind_group(11, DEV.c_str()));
    })


    .Single("poll", []
    {
        pfq::socket x;
        AssertThrow(x.poll(10));

        x.open(pfq::group_policy::undefined, 64);
        x.poll(0);
    })


    .Single("read", []
    {
        pfq::socket x;
        AssertThrow(x.read(10));

        x.open(pfq::group_policy::undefined, 64);
        AssertThrow(x.read(10));

        x.enable();
        Assert(x.read(10).empty());
    })


    .Single("stats", []
    {
        pfq::socket x;
        AssertThrow(x.stats());

        x.open(pfq::group_policy::undefined, 64);

        auto s = x.stats();
        Assert(s.recv, is_equal_to(0UL));
        Assert(s.lost, is_equal_to(0UL));
        Assert(s.drop, is_equal_to(0UL));
    })

    .Single("group_stats", []
    {
        pfq::socket x;

        x.open(pfq::group_policy::undefined, 64);

        AssertThrow(x.group_stats(11));

        x.join_group(11);

        auto s = x.group_stats(11);
        Assert(s.recv, is_equal_to(0UL));
        Assert(s.lost, is_equal_to(0UL));
        Assert(s.drop, is_equal_to(0UL));
    })


    .Single("my_group_stats_priv", []
    {
        pfq::socket x;

        x.open(pfq::group_policy::priv, 64);

        auto gid = x.group_id();

        AssertNoThrow(x.group_stats(gid));

        auto s = x.group_stats(gid);
        Assert(s.recv, is_equal_to(0UL));
        Assert(s.lost, is_equal_to(0UL));
        Assert(s.drop, is_equal_to(0UL));
    })

    .Single("my_group_stats_restricted", []
    {
        pfq::socket x;

        x.open(pfq::group_policy::restricted, 64);

        auto gid = x.group_id();

        AssertNoThrow(x.group_stats(gid));

        auto s = x.group_stats(gid);
        Assert(s.recv, is_equal_to(0UL));
        Assert(s.lost, is_equal_to(0UL));
        Assert(s.drop, is_equal_to(0UL));
    })

    .Single("my_group_stats_shared", []
    {
        pfq::socket x;

        x.open(pfq::group_policy::shared, 64);

        auto gid = x.group_id();

        AssertNoThrow(x.group_stats(gid));

        auto s = x.group_stats(gid);
        Assert(s.recv, is_equal_to(0UL));
        Assert(s.lost, is_equal_to(0UL));
        Assert(s.drop, is_equal_to(0UL));
    })

    .Single("groups_mask", []
    {
        pfq::socket x;
        AssertThrow(x.groups_mask());

        x.open(pfq::group_policy::undefined, 64);

        Assert(x.groups_mask(), is_equal_to(0UL));

        auto v = x.groups();
        Assert(v.empty(), is_true());
    })

    .Single("join_restricted", []
    {
        pfq::socket x(pfq::group_policy::restricted, 64);

        pfq::socket y;

        y.open(pfq::group_policy::undefined, 64);

        Assert( y.join_group(x.group_id(), pfq::group_policy::restricted), is_equal_to(x.group_id()));
    })

    .Single("join_deferred", []
    {
        pfq::socket x(pfq::group_policy::undefined, 64);

        x.join_group(22);
        x.join_group(22);

        auto task = std::async(std::launch::async,
                    [&] {
                        pfq::socket y(pfq::group_policy::undefined, 64);
                        Assert(y.join_group(22), is_equal_to(22));
                    });

        task.wait();
    })


    .Single("join_restricted_thread", []
    {
        pfq::socket x(pfq::group_policy::restricted, 64);

        auto task = std::async(std::launch::async,
                    [&] {
                        pfq::socket y(pfq::group_policy::undefined, 64);
                        Assert(y.join_group(x.group_id(), pfq::group_policy::restricted), is_equal_to(x.group_id()));
                    });

        task.get(); // eventually rethrow the excpetion...
    })

    .Single("join_restricted_process", []
    {
        pfq::socket x(pfq::group_policy::restricted, 64);
        pfq::socket z(pfq::group_policy::shared, 64);

        auto p = fork();
        if (p == -1)
            throw std::system_error(errno, std::generic_category());

        if (p == 0) {
            pfq::socket y(pfq::group_policy::undefined, 64);;

            Assert( y.join_group(z.group_id()), is_equal_to(z.group_id()));
            AssertThrow(y.join_group(x.group_id()));

            _Exit(1);
        }

        wait(nullptr);
    })

    .Single("join_private_", []
    {
        pfq::socket x(64);

        pfq::socket y(pfq::group_policy::undefined, 64);

        AssertThrow(y.join_group(x.group_id(), pfq::group_policy::restricted));
        AssertThrow(y.join_group(x.group_id(), pfq::group_policy::shared));
        AssertThrow(y.join_group(x.group_id(), pfq::group_policy::priv));
        AssertThrow(y.join_group(x.group_id(), pfq::group_policy::undefined));
    })

    .Single("join_restricted_", []
    {
        {
            pfq::socket x(pfq::group_policy::restricted, 64);
            pfq::socket y(pfq::group_policy::undefined, 64);
            AssertNoThrow(y.join_group(x.group_id(), pfq::group_policy::restricted));
        }
        {
            pfq::socket x(pfq::group_policy::restricted, 64);
            pfq::socket y(pfq::group_policy::undefined, 64);
            AssertThrow(y.join_group(x.group_id(), pfq::group_policy::shared));
        }
        {
            pfq::socket x(pfq::group_policy::restricted, 64);
            pfq::socket y(pfq::group_policy::undefined, 64);
            AssertThrow(y.join_group(x.group_id(), pfq::group_policy::priv));
        }
        {
            pfq::socket x(pfq::group_policy::restricted, 64);
            pfq::socket y(pfq::group_policy::undefined, 64);
            AssertThrow(y.join_group(x.group_id(), pfq::group_policy::undefined));
        }
    })

    .Single("join_shared_", []
    {
        {
            pfq::socket x(pfq::group_policy::shared, 64);
            pfq::socket y(pfq::group_policy::undefined, 64);
            AssertThrow(y.join_group(x.group_id(), pfq::group_policy::restricted));
        }
        {
            pfq::socket x(pfq::group_policy::shared, 64);
            pfq::socket y(pfq::group_policy::undefined, 64);
            AssertNoThrow(y.join_group(x.group_id(), pfq::group_policy::shared));
        }
        {
            pfq::socket x(pfq::group_policy::shared, 64);
            pfq::socket y(pfq::group_policy::undefined, 64);
            AssertThrow(y.join_group(x.group_id(), pfq::group_policy::priv));
        }
        {
            pfq::socket x(pfq::group_policy::shared, 64);
            pfq::socket y(pfq::group_policy::undefined, 64);
            AssertThrow(y.join_group(x.group_id(), pfq::group_policy::undefined));
        }
    })


    .Single("join_public", []
    {
        pfq::socket x;
        AssertThrow(x.join_group(12));

        x.open(pfq::group_policy::undefined, 64);
        int gid = x.join_group(0);
        Assert(gid, is_equal_to(0));

        gid = x.join_group(pfq::any_group);
        Assert(gid, is_equal_to(1));

        auto v = x.groups();

        Assert( v == (std::vector<int>{ 0, 1}) );

    })

    .Single("leave_group", []
    {
        pfq::socket x;
        AssertThrow(x.leave_group(12));

        x.open(pfq::group_policy::shared, 64);
        int gid = x.join_group(22);
        Assert(gid, is_equal_to(22));

        x.leave_group(22);

        Assert(x.group_id(), is_equal_to(0));
        Assert(x.groups() == std::vector<int>{ 0 });
    })


    .Single("gid", []
    {
        pfq::socket x;
        Assert(x.group_id(), is_equal_to(-1));
    })


    .Single("vlan_enable", []
    {
        pfq::socket x(64);
        AssertNoThrow(x.vlan_filters_enable(x.group_id(), true));
        AssertNoThrow(x.vlan_filters_enable(x.group_id(), false));
    })

    .Single("vlan_filt", []
    {
        pfq::socket x(64);
        AssertThrow(x.vlan_set_filter(x.group_id(), 22));
        AssertThrow(x.vlan_reset_filter(x.group_id(), 22));

        AssertNoThrow(x.vlan_filters_enable(x.group_id(), true));
        AssertNoThrow(x.vlan_set_filter(x.group_id(), 22));
        AssertNoThrow(x.vlan_reset_filter(x.group_id(), 22));

        AssertNoThrow(x.vlan_filters_enable(x.group_id(), false));
    })


    .Single("bind_tx", []
    {
        pfq::socket q(64);
        AssertNoThrow(q.bind_tx("lo", -1));
        AssertThrow(q.bind_tx("unknown", -1));
    })


    .Single("tx_thread", []
    {
        pfq::socket q(64);

        q.bind_tx("lo", -1);

        q.enable();
    })


    .Single("tx_queue_flush", []
    {
        pfq::socket q(64);
        AssertThrow(q.tx_queue_flush());

        q.bind_tx("lo", -1);

        q.enable();

        AssertNoThrow(q.tx_queue_flush());
    })

    .Single("egress_bind", []
    {
        pfq::socket q(64);
        AssertNoThrow(q.egress_bind("lo", -1));
        AssertThrow(q.egress_bind("unknown", -1));
    })

    .Single("egress_unbind", []
    {
        pfq::socket q(64);
        AssertNoThrow(q.egress_unbind());
    });

#if 0
    .Single("group_context", []
    {
        pfq::socket x(pfq::group_policy::shared, 64);

        int n = 22;
        x.set_group_function_context(x.group_id(), n);

        int m = 0;
        x.get_group_function_context(x.group_id(), m);

        Assert(n, is_equal_to(m));

        x.bind(DEV.c_str());

        x.set_group_function(x.group_id(), "dummy",  0);
        x.set_group_function(x.group_id(), "clone",  1);

        x.enable();

        std::cout << "waiting for packets from " << DEV << "..." << std::flush;
        for(;;)
        {
            auto q = x.read(100000);
            if (q.empty())
            {
                std::cout << "." << std::flush;
                continue;
            }

            for(auto &hdr : q)
            {
                Assert(hdr.data, is_equal_to(22));
                Assert(hdr.gid,  is_equal_to(x.group_id()));
            }
            break;
        }
        std::cout << std::endl;
    })

#endif



int main(int argc, char *argv[])
{
    return yats::run(argc, argv);
}
