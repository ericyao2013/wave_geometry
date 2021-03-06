/**
 * @file
 */

#ifndef WAVE_GEOMETRY_PREPAREOUTPUT_HPP
#define WAVE_GEOMETRY_PREPAREOUTPUT_HPP

namespace wave {
namespace internal {

/** Prepare an expression tree with the given Target, and initialize an Evaluator
 *
 * This function is enabled when the Destination type is already produced by the
 * expression, so no additional Convert is applied to the root. However, the expression is
 * modified according to the PreparedType of each node.
 *
 * @returns an Evaluator of the expression's PreparedType
 * @note The expression stored in `prepareEvaluatorTo<T>(expr).expr`) is *not*
 * necessarily the same as the input `expr`.
 */
template <
  typename Destination,
  typename Derived,
  tmp::enable_if_t<std::is_same<Destination, eval_output_t<arg_t<Derived>>>{}, int> = 0>
WAVE_STRONG_INLINE auto prepareEvaluatorTo(Derived &&expr)
  -> Evaluator<typename traits<arg_t<Derived>>::PreparedType> {
    // First, transform the expression
    const auto &evaluable_expr =
      PrepareExpr<arg_t<Derived>>::run(std::forward<Derived>(expr));
    using ExprType = tmp::remove_cr_t<decltype(evaluable_expr)>;

    static_assert(std::is_same<ExprType, typename traits<arg_t<Derived>>::PreparedType>{},
                  "Internal sanity check");

    // Construct Evaluator tree
    return internal::Evaluator<ExprType>{evaluable_expr};
}

/** Prepare an expression tree with the given Target, and initialize an Evaluator.
 *
 * Applies a conversion to the root of the tree to produce the desired Destination type,
 * then modifies it according to the PreparedType of each node.
 *
 * @returns an Evaluator of the expression's PreparedType
 * @note The expression stored in `prepareEvaluatorTo<T>(expr).expr`) is *not*
 * necessarily the same as the input `expr`.
 */
template <
  typename Destination,
  typename Derived,
  tmp::enable_if_t<!std::is_same<Destination, eval_output_t<arg_t<Derived>>>{}, int> = 0>
WAVE_STRONG_INLINE auto prepareEvaluatorTo(Derived &&expr) -> Evaluator<
  typename traits<Convert<eval_t<Destination>, arg_t<Derived>>>::PreparedType> {
    // Add the needed conversion
    using ConvertedType = Convert<eval_t<Destination>, arg_t<Derived>>;

    auto converted_expr = ConvertedType{std::forward<Derived>(expr)};

    // Assert we will call the other version of prepareEvaluatorTo() now:
    static_assert(std::is_same<Destination, eval_output_t<ConvertedType>>{},
                  "Internal sanity check");

    return prepareEvaluatorTo<Destination>(std::move(converted_expr));
}

/** Applies output functor to the result of an evaluator tree
 * (e.g. wraps in Framed<...>)
 */
template <typename Derived, typename EvalLeaf>
auto prepareLeafForOutput(EvalLeaf &&leaf) -> output_t<Derived, EvalLeaf> {
    const auto &functor = typename internal::traits<Derived>::OutputFunctor{};
    return functor(std::forward<EvalLeaf>(leaf));
};

/** Applies output functor to the result of an evaluator, obtaining it from the evaluator
 * (e.g. wraps in Framed<...>)
 */
template <typename Derived>
auto prepareOutput(const Evaluator<Derived> &evaluator)
  -> decltype(prepareLeafForOutput<Derived>(evaluator())) {
    return prepareLeafForOutput<Derived>(evaluator());
};

/** Evaluates an expression tree into the given type
 */
template <typename Destination, typename Derived>
auto evaluateTo(Derived &&expr) -> Destination {
    // Construct Evaluator tree
    const auto &evaluator = prepareEvaluatorTo<Destination>(std::forward<Derived>(expr));

    // Evaluate and apply output functor (e.g. wrap in Framed)
    return prepareOutput(evaluator);
}

}  // namespace internal


/** Evaluates an expression tree into a leaf.
 *
 * @returns a leaf expression of user-facing "plain-old-object" type.
 */
template <typename Derived>
auto eval(const ExpressionBase<Derived> &expr) -> internal::plain_output_t<Derived> {
    return expr.derived().eval();
}


}  // namespace wave

#endif  // WAVE_GEOMETRY_PREPAREOUTPUT_HPP
