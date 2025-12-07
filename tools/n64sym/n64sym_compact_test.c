#define STB_DS_IMPLEMENTATION
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/stb_ds.h"
#include "n64sym_compact.h"

typedef struct {
    const char *input;
    int max_len;
    const char *expected;
    int use_compact; // 1 => compact_symbol, 0 => simple_truncate, 2 => head_tail_ellipsis in place
} test_case_t;

static test_case_t tests[] = {
    {"foo::bar::Baz", 16, "f::b::Baz", 1},
    {"std::vector<std::pair<int, std::string>, std::allocator<std::pair<int,std::string>>>", 40, "s::vec<pair<i,s::str>,...>", 1},
    {"very_long_namespace::ClassName::Method(int, long long, double)", 32, "v::C::Method(i,ll,d)", 1},
    {"std::__new_allocator<Scene::ActorSpawnReq>::deallocate(Scene::ActorSpawnReq*, unsigned int)", 64, "s::new_alloc<S::ActorSpawnReq>::deallocate(S::ActorSpawnReq*,ui)", 1},
    {"__gnu_cxx::__normal_iterator<Coll::Sphere**, std::vector<Coll::Sphere*, std::allocator<Coll::Sphere*> > > std::__copy_move_a<true, __gnu_cxx::__normal_iterator<Coll::Sphere**, std::vector<Coll::Sphere*, std::allocator<Coll::Sphere*> > >, __gnu_cxx::__normal_iterator<Coll::Sphere**, std::vector<Coll::Sphere*, std::allocator<Coll::Sphere*> > > >(__gnu_cxx::__normal_iterator<Coll::Sphere**, std::vector<Coll::Sphere*, std::allocator<Coll::Sphere*> > >, __gnu_cxx::__normal_iterator<Coll::Sphere**, std::vector<Coll::Sphere*, std::allocator<Coll::Sphere*> > >, __gnu_cxx::__normal_iterator<Coll::Sphere**, std::vector<Coll::Sphere*, std::allocator<Coll::Sphere*> > >)", 64, "iter<C::Sphere**> s::copy_move<true>(iter<C::Sphere**>,...)", 1},
    {"std::_Function_handler<void (unsigned long), Demo::RDPFillTri::draw()::{lambda(unsigned long)#1}>::_M_invoke(std::_Any_data const&, unsigned long&&)", 64, "s::fn<void,lambda#1>::_M_invoke(s::_Any const&,ul&&)", 1},
    {"(anonymous namespace)::generic_error_category::~generic_error_category()", 64, "(anon)::generic_error_category::~generic_error_category()", 1},
    {"MiMem::writeAligned(unsigned long long volatile*, unsigned long long, int)", 64, "MiMem::writeAligned(ull volatile*,ull,i)", 1},
    {"std::pair<std::__detail::_Node_iterator<std::pair<unsigned long long const, AudioManager::SFX>, false, false>, bool> std::__detail::_Insert<unsigned long long, std::pair<unsigned long long const, AudioManager::SFX>, std::allocator<std::pair<unsigned long long const, AudioManager::SFX> >, std::__detail::_Select1st, std::equal_to<unsigned long long>, std::hash<unsigned long long>, std::__detail::_Mod_range_hashing, std::__detail::_Default_ranged_hash, std::__detail::_Prime_rehash_policy, std::__detail::_Hashtable_traits<false, false, true>, false>::insert<std::pair<unsigned long long const, AudioManager::SFX>, void>(std::pair<unsigned long long const, AudioManager::SFX>&&)", 64, "pair<niter<pair<ull const,A::SFX>>,b> insert(...const,A::SFX>&&)", 1},
    {"abcdefghijklmnopqrstuvwxyz", 10, "abcdefg...", 0},
    {"abcdefghijklmnopqrstuvwxyz0123456789", 12, "a...23456789", 2},
    {"std::_Hashtable<unsigned long long, std::pair<unsigned long long const, AudioManager::SFX>, std::allocator<std::pair<unsigned long long const, AudioManager::SFX> >, std::__detail::_Select1st, std::equal_to<unsigned long long>, std::hash<unsigned long long>, std::__detail::_Mod_range_hashing, std::__detail::_Default_ranged_hash, std::__detail::_Prime_rehash_policy, std::__detail::_Hashtable_traits<false, false, true> >::clear()", 64, "s::_Hashtable<ull,...>::clear()", 1},
    {"std::unordered_map<unsigned long long, AudioManager::SFX, std::hash<unsigned long long>, std::equal_to<unsigned long long>, std::allocator<std::pair<unsigned long long const, AudioManager::SFX> > >::~unordered_map()", 64, "s::umap<ull,...,SFX>::~umap()", 1},
    {NULL, 0, NULL, 0}
};

int main(void)
{
    int failures = 0;
    for (int i = 0; tests[i].input; i++) {
        const test_case_t *t = &tests[i];
        char *out = NULL;
        if (t->use_compact == 1) {
            out = compact_symbol(t->input, t->max_len);
        } else if (t->use_compact == 0) {
            out = simple_truncate(t->input, t->max_len);
        } else if (t->use_compact == 2) {
            char *buf = strdup(t->input);
            head_tail_ellipsis(buf, t->max_len);
            out = buf;
        }
        if (!out) {
            printf("FAIL: %s (alloc error)\n", t->input);
            failures++;
            continue;
        }
        int ok_len = (int)strlen(out) <= t->max_len;
        if (!ok_len || strcmp(out, t->expected) != 0) {
            printf("FAIL: \"%s\" max %d -> \"%s\" (expected \"%s\")\n",
                   t->input, t->max_len, out, t->expected);
            failures++;
        } else {
            printf("OK  : \"%s\" max %d -> \"%s\"\n", t->input, t->max_len, out);
        }
        free(out);
    }
    return failures ? 1 : 0;
}

