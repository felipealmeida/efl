// -*- mode: C++ -*-

#define I BOOST_PP_SUB(BOOST_PP_ITERATION(), 1)

  detail::initialize_operation_description
    <inherit<D, P, BOOST_PP_ENUM_PARAMS(EFL_MAX_ARGS, E)> >
    (detail::tag<BOOST_PP_CAT(E, I)>()
    , &op_descs[detail::operation_description_size<P BOOST_PP_ENUM_TRAILING_PARAMS(BOOST_PP_SUB(I, 1), E)>::value]);
