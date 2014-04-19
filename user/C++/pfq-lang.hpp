/***************************************************************
 *
 * (C) 2011-13 Nicola Bonelli <nicola.bonelli@cnit.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 ****************************************************************/

#pragma once

#include <sstream>
#include <string>
#include <cstring>
#include <vector>
#include <memory>
#include <type_traits>

#include <linux/pf_q.h>


namespace pfq_lang
{
    using ::pfq_functional_type;
    using ::pfq_functional_descr;

    static inline std::string
    show(enum pfq_functional_type ft)
    {
        switch(ft)
        {
            case pfq_monadic_fun:    return "fun";
            case pfq_high_order_fun: return "hfun";
            case pfq_predicate_fun:  return "pred";
            case pfq_combinator_fun: return "comb";
        }

        throw std::logic_error("unknown type");
    }

    static inline std::string
    show(pfq_functional_descr const &descr)
    {
        std::stringstream out;

        out << "functional_descr "
            << "type:"      << show (descr.type) << ' '
            << "symbol:"    << descr.symbol     << ' '
            << "arg_ptr:"   << descr.arg_ptr    << ' '
            << "arg_size:"  << descr.arg_size   << ' '
            << "left:"      << descr.l_index    << ' '
            << "right:"     << descr.r_index;

        return out.str();
    }

    //
    // Functional descriptor
    //

    struct FunDescr
    {
        enum pfq_functional_type    type;
        std::string                 symbol;

        std::shared_ptr<void>       arg_ptr;
        size_t                      arg_size;

        int                         left;
        int                         right;
    };

    static inline std::string
    show(FunDescr const &descr)
    {
        return "FunDescr { " + show(descr.type) + ' '
                             + descr.symbol + ' '
                             + std::to_string((unsigned long)descr.arg_ptr.get()) + ' '
                             + std::to_string(descr.arg_size) + ' '
                             + std::to_string(descr.left) + ' '
                             + std::to_string(descr.right) + " }";
    }

    static inline void
    relinkFunDescr(int n, int m, FunDescr &descr)
    {
        if (descr.left == n)
            descr.left = m;
        if (descr.right == n)
            descr.right = m;
    }

    ///////////////////////////////////////////////////

    namespace term
    {
        //////// is_same_template:

        template <typename T, template <typename ...> class Tp>
        struct is_same_template : std::false_type
        { };

        template <template <typename ...> class Tp, typename ...Ti>
        struct is_same_template<Tp<Ti...>, Tp> : std::true_type
        { };

        //////// vector concat:

        template <typename Tp>
        inline std::vector<Tp>
        operator+(std::vector<Tp> v1, std::vector<Tp> &&v2)
        {
            v1.insert(v1.end(), std::make_move_iterator(v2.begin()),
                      std::make_move_iterator(v2.end()));
            return v1;
        }

        template <typename Tp>
        inline std::vector<Tp>
        operator+(std::vector<Tp> v1, std::vector<Tp> const &v2)
        {
            v1.insert(v1.end(), v2.begin(), v2.end());
            return v1;
        }

        //////// Combinator:

        struct Combinator
        {
            std::string name_;
        };

        static inline std::string
        show(Combinator const &comb)
        {
            if (comb.name_ == "or")
                return "|";
            if (comb.name_ == "and")
                return "&";
            if (comb.name_ == "xor")
                return "^";

            throw std::logic_error("combinator: internal error");
        }

        static inline std::pair<std::vector<FunDescr>, int>
        serialize(int n, Combinator const &comb)
        {
            return std::make_pair(std::vector<FunDescr>
            {
                FunDescr { pfq_combinator_fun, comb.name_, std::shared_ptr<void>(), 0, -1, -1 }
            }, n+1);
        }

        //////// Predicates:

        struct Pred
        {
            std::string name_;
        };

        struct Pred1
        {
            template <typename T>
            Pred1(std::string name, T const &arg)
            : name_(std::move(name))
            , ptr_(std::shared_ptr<void>{ new T(arg) })
            , size_(sizeof(T))
            {
            }

            std::string           name_;
            std::shared_ptr<void> ptr_;
            size_t                size_;
        };

        template <typename P1, typename P2>
        struct Pred2
        {
            Combinator      comb_;
            P1              left_;
            P2              right_;
        };

        //////// is_predicate:

        template <typename Tp>
        struct is_predicate :
            std::integral_constant<bool,
                std::is_same<Tp, term::Pred>::value  ||
                std::is_same<Tp, term::Pred1>::value ||
                is_same_template<Tp, term::Pred2>::value>
        { };

        ///////// show predicates:

        static inline std::string
        show(Pred const &descr)
        {
            return descr.name_;
        }

        static inline std::string
        show(Pred1 const &descr)
        {
            std::ostringstream out;
            out << '(' << descr.name_ << ' ' << descr.ptr_.get() << ':' << std::to_string(descr.size_) << ')';
            return out.str();
        }

        template <typename P1, typename P2>
        static inline std::string
        show(Pred2<P1,P2> const &descr)
        {
            return '(' + show(descr.left_) + ' ' + show(descr.comb_) + ' ' + show(descr.right_) + ')';
        }

        //////// serialize predicates:

        static inline std::pair<std::vector<FunDescr>, int>
        serialize(int n, Pred const &p)
        {
            return std::make_pair(std::vector<FunDescr>
            {
                FunDescr { pfq_predicate_fun, p.name_, std::shared_ptr<void>(), 0, -1, -1 }
            }, n+1);
        }

        static inline std::pair<std::vector<FunDescr>, int>
        serialize(int n, Pred1 const &p)
        {
            return std::make_pair(std::vector<FunDescr>
            {
                FunDescr { pfq_predicate_fun, p.name_, p.ptr_, p.size_, -1, -1 }
            }, n+1);
        }

        template <typename  P1, typename P2>
        static inline std::pair<std::vector<FunDescr>, int>
        serialize(int n, Pred2<P1,P2> const &p)
        {
            std::vector<FunDescr> ret, left, right;
            int n1, n2, n3;

            std::tie(ret, n1)   = serialize(n, p.comb_);
            std::tie(left, n2)  = serialize(n1, p.left_);
            std::tie(right, n3) = serialize(n2, p.left_);

            ret[0].left = n1;
            ret[0].right = n2;

            std::move(left.begin(), left.end(),   std::back_inserter(ret));
            std::move(right.begin(), right.end(), std::back_inserter(ret));

            return std::make_pair(ret, n3);
        }

        //
        // Computations:
        //

        struct Fun
        {
            std::string  name_;
        };

        struct Fun1
        {
            template <typename T>
            Fun1(std::string name, T const &arg)
            : name_(std::move(name))
            , ptr_(std::shared_ptr<void>{ new T(arg) })
            , size_(sizeof(T))
            {
            }

            std::string           name_;
            std::shared_ptr<void> ptr_;
            size_t                size_;
        };

        template <typename P>
        struct HFun
        {
            std::string  name_;
            P           pred_;
        };

        template <typename P, typename C>
        struct HFun1
        {
            std::string  name_;
            P           pred_;
            C           comp_;
        };

        template <typename P, typename C1, typename C2>
        struct HFun2
        {
            std::string  name_;
            P           pred_;
            C1          comp1_;
            C2          comp2_;
        };

        template <typename C1, typename C2>
        struct Comp
        {
            C1 comp1_;
            C2 comp2_;
        };

        template <typename Tp>
        struct is_computation :
            std::integral_constant<bool,
                std::is_same<Tp, Fun>::value  ||
                std::is_same<Tp, Fun1>::value ||
                is_same_template<Tp, HFun>::value ||
                is_same_template<Tp, HFun1>::value ||
                is_same_template<Tp, HFun2>::value ||
                is_same_template<Tp, Comp>::value>
        { };

        ///// show computations:

        static inline std::string
        show(Fun const &descr)
        {
            return descr.name_;
        }

        static inline std::string
        show(Fun1 const &descr)
        {
            std::ostringstream out;
            out << '(' << descr.name_ << ' ' << descr.ptr_.get() << ':' << std::to_string(descr.size_) << ')';
            return out.str();
        }

        template <typename P>
        static inline std::string
        show(HFun<P> const &descr)
        {
            return '(' + descr.name_ + ' ' + show(descr.pred_) + ')';
        }

        template <typename P, typename C>
        static inline std::string
        show(HFun1<P,C> const &descr)
        {
            return '(' + descr.name_ + ' ' + show(descr.pred_) + ' ' + show(descr.comp_) + ')';
        }

        template <typename P, typename C1, typename C2>
        static inline std::string
        show(HFun2<P,C1,C2> const &descr)
        {
            return '(' + descr.name_ + ' ' + show(descr.pred_) + ' ' + show(descr.comp1_) + ' ' + show(descr.comp2_) + ')';
        }

        template <typename C1, typename C2>
        static inline std::string
        show(Comp<C1,C2> const &descr)
        {
            return show(descr.comp1_) + " >-> " + show(descr.comp2_);
        }

    } // namespace term


    inline term::Combinator
    combinator(std::string name)
    {
        return term::Combinator{ std::move(name) };
    }

    inline term::Pred
    predicate(std::string name)
    {
        return term::Pred{ std::move(name) };
    }

    template <typename T>
    inline typename std::enable_if<
          std::is_pod<T>::value,
    term::Pred1>::type
    predicate(std::string name, const T &arg)
    {
        return term::Pred1{ std::move(name), arg };
    }

    template <typename P1, typename P2>
    inline typename std::enable_if<
        term::is_predicate<P1>::value &&
        term::is_predicate<P2>::value,
    term::Pred2<P1,P2>>::type
    predicate(term::Combinator c, P1 const &left, P2 const &right)
    {
        return term::Pred2<P1,P2>{ c, left, right };
    }

    inline term::Fun
    computation(std::string name)
    {
        return term::Fun{ std::move(name) };
    }

    template <typename T>
    inline typename std::enable_if<
          std::is_pod<T>::value,
    term::Fun1>::type
    computation(std::string name, const T &arg)
    {
        return term::Fun1{ std::move(name), arg };
    }

    template <typename P>
    inline typename std::enable_if<
          term::is_predicate<P>::value,
    term::HFun<P>>::type
    computation(std::string name, P const &p)
    {
        return term::HFun<P>{ std::move(name), p };
    }

    template <typename P, typename C>
    inline typename std::enable_if<
          term::is_predicate<P>::value &&
          term::is_computation<C>::value,
    term::HFun1<P,C>>::type
    computation(std::string name, P const &p, C const &c)
    {
        return term::HFun1<P,C>{ std::move(name), p, c};
    }

    template <typename P, typename C1, typename C2>
    inline typename std::enable_if<
          term::is_predicate<P>::value &&
          term::is_computation<C1>::value &&
          term::is_computation<C2>::value,
    term::HFun2<P,C1, C2>>::type
    computation(std::string name, P const &p, C1 const &c1, C2 const &c2)
    {
        return term::HFun2<P,C1,C2>{ std::move(name), p, c1, c2};
    }

    //
    // Kleisli composition: >->
    //

    template <typename C1, typename C2>
    inline typename std::enable_if<
          term::is_computation<C1>::value &&
          term::is_computation<C2>::value,
    term::Comp<C1, C2>>::type
    operator>>(C1 c1, C2 c2)
    {
        return { std::move(c1), std::move(c2) };
    }

   //  template <typename Tp, typename Fun>
   //  inline Computation<Computation<Tp>>
   //  operator>>(Computation<Tp> lhs, Fun &&rhs)
   //  {
   //      return { std::move(lhs), std::forward<Fun>(rhs) };
   //  }

    //
    // default in-kernel PFQ functions...
    //

#define PFQ_MAKE_FUN(fn, name) \
    inline QFunction fn() \
    { \
        return qfun(name); \
    }

#define PFQ_MAKE_FUN1(fn,name, typ) \
    inline QFunction fn(typ const &) \
    { \
        return qfun(name, ); \
    }
//
//     namespace
//     {
//         PFQ_MAKE_FUN(steer_mac    , "steer_mac"    )
//         PFQ_MAKE_FUN(steer_vlan   , "steer_vlan"   )
//         PFQ_MAKE_FUN(steer_ip     , "steer_ip"     )
//         PFQ_MAKE_FUN(steer_ipv6   , "steer_ipv6"   )
//         PFQ_MAKE_FUN(steer_flow   , "steer_flow"   )
//
//         PFQ_MAKE_FUN(legacy       , "legacy"       )
//         PFQ_MAKE_FUN(broadcast    , "broadcast"    )
//         PFQ_MAKE_FUN(sink         , "sink"         )
//         PFQ_MAKE_FUN(drop         , "drop"         )
//
//         PFQ_MAKE_FUN(id           , "id"           )
//
//         PFQ_MAKE_FUN(ip           , "ip"           )
//         PFQ_MAKE_FUN(ipv6         , "ipv6"         )
//         PFQ_MAKE_FUN(udp          , "udp"          )
//         PFQ_MAKE_FUN(tcp          , "tcp"          )
//         PFQ_MAKE_FUN(vlan         , "vlan"         )
//         PFQ_MAKE_FUN(icmp         , "icmp"         )
//         PFQ_MAKE_FUN(flow         , "flow"         )
//
//         PFQ_MAKE_FUN(rtp          , "rtp"          )
//         PFQ_MAKE_FUN(steer_rtp    , "steer_rtp"    )
//
//         PFQ_MAKE_FUN1(dummy       , "dummy",   int      )
//         PFQ_MAKE_FUN1(counter     , "counter", int      )
//         PFQ_MAKE_FUN1(class_      , "class",   uint16_t )
//     }

} // namespace pfq_lang
